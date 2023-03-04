//
// Copyright (c) 2010-2022 Antmicro
// Copyright (c) 2021 Google LLC
//
// This file is licensed under the MIT License.
// Full license text is available in 'licenses/MIT.txt'.
//
using System;
using System.Collections.Generic;
using Antmicro.Renode.Core.Structure.Registers;
using Antmicro.Renode.Exceptions;
using Antmicro.Renode.Logging;
using Antmicro.Renode.Peripherals.Bus;
using Antmicro.Renode.Peripherals.IRQControllers.PLIC;

namespace Antmicro.Renode.Peripherals.IRQControllers
{
    [AllowedTranslations(AllowedTranslation.ByteToDoubleWord)]
    public class OpenTitan_PlatformLevelInterruptController : PlatformLevelInterruptControllerBase, IKnownSize
    {
        public OpenTitan_PlatformLevelInterruptController(int numberOfSources = 84, int numberOfContexts = 1)
            : base(numberOfSources, numberOfContexts, prioritiesEnabled: true, extraInterrupts: (uint)numberOfContexts)
        {
            // OpenTitan PLIC implementation contains an additional register per hart.
            // Each of these memory mapped registers is intended to be connected to the MSIP bits
            // within the MIP CSR registers of the individial harts.
            // This allows interprocessor interrupts between harts.

            // OpenTitan PLIC implementation source is limited
            if(numberOfSources > MaxNumberOfSources)
            {
                throw new ConstructionException($"Current {this.GetType().Name} implementation does not support more than {MaxNumberOfSources} sources");
            }

            if(numberOfContexts > MaxNumberOfContexts)
            {
                throw new ConstructionException($"Current {this.GetType().Name} implementation does not support more than {MaxNumberOfContexts} contexts");
            }

            this.numberOfContexts = numberOfContexts;

            var registersMap = new Dictionary<long, DoubleWordRegister>();

            var interruptPendingRegisterCount = (int)Math.Ceiling(numberOfSources / 32.0);
            for(var i = 0; i < interruptPendingRegisterCount; i++)
            {
                var interruptPendingOffset = (long)Registers.InterruptPending0 + i * 4;
                this.Log(LogLevel.Noisy, "Creating InterruptPending{0}[0x{1:X}]", i, interruptPendingOffset);
                registersMap.Add(interruptPendingOffset, new DoubleWordRegister(this)
                    .WithValueField(0, 32, FieldMode.Read, valueProviderCallback: _ =>
                        {
                            // TODO: Build register value from pending sources IRQs
                            return 0;
                        }));
            }

            for(uint i = 0; i < numberOfSources; i++)
            {
                var j = i;
                var sourcePriorityOffset = (uint)Registers.InterruptPriority0 + (4u * i);
                this.Log(LogLevel.Noisy, "Creating SourcePriority{0}[0x{1:X}]", i, sourcePriorityOffset);
                registersMap.Add(sourcePriorityOffset, new DoubleWordRegister(this)
                    .WithValueField(0, 3,
                                    valueProviderCallback: _ => irqSources[j].Priority,
                                    writeCallback: (_, value) =>
                                    {
                                        irqSources[j].Priority = value;
                                        RefreshInterrupts();
                                    })
                     .WithReservedBits(3, 29));
            }

            var contextMachineEnableCount = (int)Math.Ceiling(numberOfSources / 32.0);
            this.Log(LogLevel.Noisy, $"ContextMachineEnableCount 0x{contextMachineEnableCount:X}");

            for(uint i = 0; i < numberOfContexts; i++)
            {
                var contextMachineEnablesOffset = (long)Registers.InterruptEnable0 + (i * ContextRegistersOffset);
                this.Log(LogLevel.Noisy, $"ContextMachineEnablesOffset for Context{i} 0x{contextMachineEnablesOffset:X}");
                AddContextEnablesRegister(registersMap, contextMachineEnablesOffset, i, numberOfSources - 1);

                var contextPriorityThresholdOffset = (uint) Registers.Target0Threshold + (i * GeneralContextRegistersOffset);
                AddContextPriorityThresholdRegister(registersMap, contextPriorityThresholdOffset, i);

                var contextClaimCompleteOffset = contextPriorityThresholdOffset + 0x4;
                AddContextClaimCompleteRegister(registersMap, contextClaimCompleteOffset, i);

                var contextSoftwareOffset = (uint)Registers.Target0SoftwareInterrupt + (i * 0x4);
                AddContextSoftwareInterruptRegister(registersMap, contextSoftwareOffset, i);
            }

            registers = new DoubleWordRegisterCollection(this, registersMap);
        }

        public long Size => 0x8000000;

        protected void AddContextPriorityThresholdRegister(Dictionary<long, DoubleWordRegister> registersMap, long offset, uint hartId)
        {
            this.Log(LogLevel.Noisy, "Adding Context {0} threshold priority register address 0x{1:X}", hartId, offset);
            registersMap.Add(offset, new DoubleWordRegister(this)
                .WithValueField(0, 32,
                    valueProviderCallback: _ =>
                    {
                        // TODO: implement
                        return 0;
                    },
                    writeCallback: (_, value) =>
                    {
                        // TODO: implement
                        this.Log(LogLevel.Noisy, "Setting priority threshold for Context {0} not supported", hartId);
                    }));
        }

        protected void AddContextSoftwareInterruptRegister(Dictionary<long, DoubleWordRegister> registersMap, long offset, uint hartId)
        {
            this.Log(LogLevel.Noisy, "Adding Context {0} software interrupt register address 0x{1:X}", hartId, offset);
            registersMap.Add(offset, new DoubleWordRegister(this)
                .WithFlag(0,
                    valueProviderCallback: _ =>
                    {
                        return Connections[Connections.Count - numberOfContexts + (int)hartId].IsSet;
                    },
                    writeCallback: (_, value) =>
                    {
                        this.Log(LogLevel.Noisy, "Setting software interrupt for Context {0} to {1}", hartId, value);
                        Connections[Connections.Count - numberOfContexts + (int)hartId].Set(value);
                    })
                .WithReservedBits(1, 31));
        }

        private readonly int numberOfContexts;

        private const int MaxNumberOfSources = 255;

        private const int MaxNumberOfContexts = ((int)Registers.InterruptEnable0 - (int)Registers.InterruptPending0) / ContextRegistersOffset;

        private const int ContextRegistersOffset = 0x100;

        private const int GeneralContextRegistersOffset = 0x1000;

#pragma warning disable format
        private enum Registers : long
        {
            InterruptPriority0       = 0x0,
            InterruptPending0        = 0x1000,
            InterruptEnable0         = 0x2000,
            Target0Threshold         = 0x200000,
            Target0InterruptClaim    = 0x200004,
            Target0SoftwareInterrupt = 0x4000000,
            AlertTestRegister        = 0x4004000,
        }
#pragma warning restore format
    }
}
