//
// Copyright (c) 2010-2022 Antmicro
//
// This file is licensed under the MIT License.
// Full license text is available in 'licenses/MIT.txt'.
//
﻿using System;
using System.Collections.Generic;
using Antmicro.Renode.Core;
using Antmicro.Renode.Core.Structure.Registers;
using Antmicro.Renode.Peripherals.Bus;
using Antmicro.Renode.Time;
using Antmicro.Renode.Logging;

namespace Antmicro.Renode.Peripherals.Timers
{
    // This class does not implement advanced-control timers interrupts
    [AllowedTranslations(AllowedTranslation.ByteToDoubleWord | AllowedTranslation.WordToDoubleWord)]
    public class STM32L0_LpTimer : LimitTimer, IDoubleWordPeripheral, IKnownSize
    {
        public STM32L0_LpTimer(Machine machine, long frequency) : base(machine.ClockSource, frequency, limit: 0x1, direction: Direction.Ascending, enabled: false, eventEnabled: true)
        {
            IRQ = new GPIO();

            LimitReached += () =>
            {
                this.Log(LogLevel.Debug, "Limit reached");
                autoReloadMatchInterruptStatus.Value = true;
                UpdateInterrupts();
            };

            var registersMap = new Dictionary<long, DoubleWordRegister>
            {
                 {(long)Registers.InterruptAndStatus, new DoubleWordRegister(this)
                    .WithTaggedFlag("Compare match (CMPM)", 0)
                    .WithFlag(1, out autoReloadMatchInterruptStatus, FieldMode.Read, name: "Autoreload match (ARRM)")
                    .WithTaggedFlag("External trigger edge event (EXTTRIG)", 2)
                    .WithFlag(3, out compareRegisterUpdateOkStatus, FieldMode.Read, name: "Compare register update OK (CMPOK)")
                    .WithFlag(4, out autoReloadRegisterUpdateOkStatus, FieldMode.Read, name: "Autoreload register update OK (ARROK)")
                    .WithTaggedFlag("Counter direction change down to up (UP)", 5)
                    .WithTaggedFlag("Counter direction change up to down (DOWN)", 6)
                    .WithReservedBits(7, 25)
                  },

                  {(long)Registers.InterruptClear, new DoubleWordRegister(this)
                    .WithTaggedFlag("Compare match clear flag (CMPMCF)", 0)
                    .WithFlag(1, FieldMode.WriteOneToClear,
                        writeCallback: (_, val) =>
                        {
                            if(!val)
                            {
                                return;
                            }

                            autoReloadMatchInterruptStatus.Value = false;
                            UpdateInterrupts();
                        },
                        name: "Autoreload match clear flag (ARRMCF)")
                    .WithTaggedFlag("External trigger edge event clear flag (EXTTRIGCF)", 2)
                    .WithFlag(3, FieldMode.WriteOneToClear,
                        writeCallback: (_, val) =>
                        {
                            if(!val)
                            {
                                return;
                            }
                            compareRegisterUpdateOkStatus.Value = false;
                            UpdateInterrupts();
                        },
                        name: "Compare register update OK clear flag (CMPOKCF)")
                    .WithFlag(4, FieldMode.WriteOneToClear,
                        writeCallback: (_, val) =>
                        {
                            if(!val)
                            {
                                return;
                            }
                            autoReloadRegisterUpdateOkStatus.Value = false;
                            UpdateInterrupts();
                        },
                        name: "Autoreload register update OK clear flag (ARROKCF)")
                    .WithTaggedFlag("Counter direction change down to up clear flag (UPCF)", 5)
                    .WithTaggedFlag("Counter direction change up to down clear flag (DOWNCF)", 6)
                    .WithReservedBits(7, 25)
                },

                // Caution: The LPTIM_IER register must only be modified when the LPTIM is disabled (ENABLE bit reset to '0')
                {(long)Registers.InterruptEnable, new DoubleWordRegister(this)
                    .WithTaggedFlag("Compare match interrupt enable (CMPMIE)", 0)
                    .WithFlag(1, out autoReloadMatchInterruptEnable, name: "Autoreload match interrupt enable (ARRMIE)")
                    .WithTaggedFlag("External trigger edge event interrupt enable (EXTTRIGIE)", 2)
                    .WithFlag(3, out compareRegisterUpdateOkEnable, name: "Compare register update OK interrupt enable (CMPOKIE)")
                    .WithFlag(4, out autoReloadRegisterUpdateOkEnable, name: "Autoreload register update OK interrupt enable (ARROKIE)")
                    .WithFlag(5, name: "Counter direction change down to up interrupt enable (UPIE)")
                    .WithFlag(6, name: "Counter direction change up to down interrupt enable (DOWNIE)")
                    .WithReservedBits(7, 25)
                },

                // Caution: The LPTIM_CFGR register must only be modified when the LPTIM is disabled (ENABLE bit reset to '0')
                {(long)Registers.Configuration, new DoubleWordRegister(this)
                    .WithTaggedFlag("Clock selector (CKSEL)", 0)
                    .WithTag("Clock Polarity (CKPOL)", 1, 2)
                    .WithTag("Configurable digital filter for external clock (CKFLT)", 3, 2)
                    .WithReservedBits(5, 1)
                    .WithTag("Configurable digital filter for trigger (TRGFLT)", 6, 2)
                    .WithReservedBits(8, 1)
                    .WithValueField(9, 3,
                        writeCallback: (_, val) => Divider = (int)Math.Pow(2, val),
                        valueProviderCallback: _ => (uint)Math.Log(Divider, 2),
                        name: "Clock prescaler (PSC)")
                    .WithReservedBits(12, 1)
                    .WithTag("Trigger Selector (TRIGSEL)", 13, 3)
                    .WithReservedBits(16, 1)
                    .WithTag("Trigger Enable and Polarity (TRIGEN)", 17, 2)
                    .WithTaggedFlag("Timeout enable (TIMOUT)", 19)
                    .WithTaggedFlag("Waveform Shape (WAVE)", 20)
                    .WithTaggedFlag("Waveform shape polarity (WAVPOL)", 21)
                    .WithTaggedFlag("Registers update mode (PRELOAD)", 22)
                    .WithTaggedFlag("Counter mode enabled (COUNTMODE)", 23)
                    .WithTaggedFlag("Encoder mode enable (ENC)", 24)
                    .WithReservedBits(25, 7)
                },

                {(long)Registers.Control, new DoubleWordRegister(this)
                    .WithFlag(0, out enabled, name: "LPTIM enable (ENABLE)")
                    .WithFlag(1, out var singleStart, name: "LPTIM start in Single mode (SNGSTRT)")
                    .WithFlag(2, out var continousStart, name: "Timer start in Continuous mode (CNTSTRT)")
                    .WithReservedBits(3, 29)
                    .WithWriteCallback((_, __) =>
                    {
                        if(enabled.Value)
                        {
                            if(singleStart.Value && continousStart.Value)
                            {
                                this.Log(LogLevel.Warning, "Selected both single and contiuous modes. Ignoring operation");
                                singleStart.Value = false;
                                continousStart.Value = false;
                                return;
                            }

                            if(singleStart.Value)
                            {
                                this.Log(LogLevel.Debug, "Enabling timer in the single shot mode");
                                Mode = WorkMode.OneShot;
                                Enabled = true;
                            }

                            if(continousStart.Value)
                            {
                                this.Log(LogLevel.Debug, "Enabling timer in the continous mode");
                                Mode = WorkMode.Periodic;
                                Enabled = true;
                            }
                        }
                        else
                        {
                            this.Log(LogLevel.Debug, "Disabling timer");
                            this.Enabled = false;
                        }
                    })
                },

                // Caution: The LPTIM_CMP register must only be modified when the LPTIM is enabled (ENABLE bit set to '1').
                {(long)Registers.Compare, new DoubleWordRegister(this)
                    .WithValueField(0, 16, name: "Compare value (CMP)",
                            valueProviderCallback: _ => (uint)this.Limit,
                            writeCallback: (_, val) =>
                            {
                                this.Limit = val;
                                this.Value = 0;
                                compareRegisterUpdateOkEnable.Value = true;
                                UpdateInterrupts();
                            })
                    .WithReservedBits(16, 16)
                },

                // Caution: The LPTIM_ARR register must only be modified when the LPTIM is enabled (ENABLE bit set to '1').
                {(long)Registers.AutoReload, new DoubleWordRegister(this, 0x1)
                    .WithValueField(0, 16,
                        writeCallback: (_, val) =>
                        {
                            Limit = val;
                            autoReloadRegisterUpdateOkStatus.Value = true;
                            UpdateInterrupts();
                        },
                        valueProviderCallback: _ => (uint)Limit,
                        name: "Autoreload register  (ARR)")
                    .WithReservedBits(16, 16)
                },

                {(long)Registers.Counter, new DoubleWordRegister(this)
                    .WithValueField(0, 16, FieldMode.Read, name: "Counter value (CNT)",
                            valueProviderCallback: _ => (uint)this.Value)
                    .WithReservedBits(16, 16)
                }
            };

            registers = new DoubleWordRegisterCollection(this, registersMap);
            Reset();
        }

        public uint ReadDoubleWord(long offset)
        {
            return registers.Read(offset);
        }

        public void WriteDoubleWord(long offset, uint value)
        {
            registers.Write(offset, value);
        }

        public override void Reset()
        {
            base.Reset();
            registers.Reset();
            IRQ.Set(false);
        }

        public GPIO IRQ { get; }

        public long Size => 0x400;

        private void UpdateInterrupts()
        {
            var flag = false;

            flag |= autoReloadMatchInterruptEnable.Value && autoReloadMatchInterruptStatus.Value;
            flag |= autoReloadRegisterUpdateOkEnable.Value && autoReloadRegisterUpdateOkStatus.Value;
            flag |= compareRegisterUpdateOkEnable.Value && compareRegisterUpdateOkStatus.Value;

            this.Log(LogLevel.Debug, "Setting IRQ to {0}", flag);
            IRQ.Set(flag);
        }

        private readonly DoubleWordRegisterCollection registers;

        private readonly IFlagRegisterField autoReloadMatchInterruptEnable;
        private readonly IFlagRegisterField autoReloadMatchInterruptStatus;
        private readonly IFlagRegisterField autoReloadRegisterUpdateOkEnable;
        private readonly IFlagRegisterField autoReloadRegisterUpdateOkStatus;
        private readonly IFlagRegisterField compareRegisterUpdateOkEnable;
        private readonly IFlagRegisterField compareRegisterUpdateOkStatus;
        private readonly IFlagRegisterField enabled;

        private enum Registers : long
        {
            // LPTIM_ISR
            InterruptAndStatus = 0x0,
            // LPTIM_ICR
            InterruptClear = 0x04,
            // LPTIM_IER
            InterruptEnable = 0x08,
            // LPTIM_CFGR
            Configuration = 0x0C,
            // LPTIM_CR
            Control = 0x10,
            // LPTIM_CMP
            Compare = 0x14,
            // LPTIM_ARR
            AutoReload = 0x18,
            // LPTIM_CNT
            Counter = 0x1C,
        }
    }
}

