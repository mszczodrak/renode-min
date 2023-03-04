//
// Copyright (c) 2010-2021 Antmicro
// Copyright (c) 2011-2015 Realtime Embedded
//
// This file is licensed under the MIT License.
// Full license text is available in 'licenses/MIT.txt'.
//
using System;
using Antmicro.Renode.Peripherals.Bus;
using Antmicro.Renode.Logging;
using Antmicro.Renode.Core;
using Antmicro.Renode.Utilities;

namespace Antmicro.Renode.Peripherals.Bus.Wrappers
{
    public sealed class WriteLoggingWrapper<T> : WriteHookWrapper<T>
    {
        public WriteLoggingWrapper(IBusPeripheral peripheral, Action<long, T> originalMethod) : base(peripheral, originalMethod, null, null)
        {
            mapper = new RegisterMapper(peripheral.GetType());
            machine = peripheral.GetMachine();
            needsByteSwapForDisplay = !peripheral.IsHostEndian();
        }

        public override void Write(long offset, T value)
        {
            var valueForDisplay = needsByteSwapForDisplay ? Misc.SwapBytes(value) : value;
            Peripheral.Log(LogLevel.Info, machine.SystemBus.DecorateWithCPUNameAndPC($"Write{Name} to 0x{offset:X}{(mapper.ToString(offset, " ({0})"))}, value 0x{valueForDisplay:X}."));
            OriginalMethod(offset, value);
        }

        private readonly Machine machine;
        private readonly RegisterMapper mapper;
        private readonly bool needsByteSwapForDisplay;
    }
}
