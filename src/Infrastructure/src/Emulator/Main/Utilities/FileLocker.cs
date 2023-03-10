//
// Copyright (c) 2010-2022 Antmicro
//
// This file is licensed under the MIT License.
// Full license text is available in 'licenses/MIT.txt'.
//
using System;

namespace Antmicro.Renode.Utilities
{
    public class FileLocker : IDisposable
    {
        public FileLocker(string fileToLock)
        {
            innerLocker = new PosixFileLocker(fileToLock);
        }
        
        public void Dispose()
        {
            innerLocker?.Dispose();
            innerLocker = null;
        }

        private IDisposable innerLocker;
    }
}
