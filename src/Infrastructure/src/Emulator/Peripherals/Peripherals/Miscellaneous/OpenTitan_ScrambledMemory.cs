//
// Copyright (c) 2010-2022 Antmicro
//
// This file is licensed under the MIT License.
// Full license text is available in 'licenses/MIT.txt'.
//

using System.Collections.Generic;
using Antmicro.Renode.Core;
using Antmicro.Renode.Peripherals.CPU;
using Antmicro.Renode.Peripherals.Memory;

namespace Antmicro.Renode.Peripherals.Miscellaneous
{
    public class OpenTitan_ScrambledMemory
    {
        public OpenTitan_ScrambledMemory(Machine machine, long size)
        {
            if(size % PageSize != 0)
            {
                size += (PageSize - (size % PageSize));
            }

            this.machine = machine;
            underlyingMemory = new MappedMemory(machine, size);
            writtenOffsets = new HashSet<long>();
        }

        public uint ReadDoubleWord(long offset)
        {
            if(!CheckAccess(offset, 4))
            {
                return 0;
            }
            return underlyingMemory.ReadDoubleWord(offset);
        }

        public void WriteDoubleWord(long offset, uint value)
        {
            MarkWritten(offset, 4);
            underlyingMemory.WriteDoubleWord(offset, value);
        }

        public void WriteBytes(long offset, byte[] value)
        {
            MarkWritten(offset, value.Length);
            underlyingMemory.WriteBytes(offset, value);
        }

        public void ReadBytes(long offset, int count, byte[] destination, int startIndex)
        {
            if(!CheckAccess(offset , count))
            {
                return;
            }
            underlyingMemory.ReadBytes(offset, count, destination, startIndex);
        }

        public void ZeroAll()
        {
            wasCleared = true;
            writtenOffsets.Clear();
            underlyingMemory.ZeroAll();
        }

        public long Size => underlyingMemory.Size;

        public IEnumerable<IMappedSegment> MappedSegments => underlyingMemory.MappedSegments;

        private void MarkWritten(long offset, int size = 1)
        {
            for(var i = 0; i < size; i++)
            {
                writtenOffsets.Add(offset + i);
            }
        }

        private bool CheckAccess(long offset, int size = 1)
        {
            if(wasCleared && !writtenOffsets.Contains(offset))
            {
                if(machine.SystemBus.TryGetCurrentCPU(out var cpu) && cpu is TranslationCPU translationCPU)
                {
                    translationCPU.RaiseException(0x5);
                }
                return false;
            }
            return true;
        }

        private bool wasCleared;

        private readonly Machine machine;
        private readonly MappedMemory underlyingMemory;
        private readonly HashSet<long> writtenOffsets;

        private const long PageSize = 0x1000;
    }
}
