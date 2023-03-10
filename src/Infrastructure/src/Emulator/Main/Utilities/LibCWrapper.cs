//
// Copyright (c) 2010-2021 Antmicro
//
// This file is licensed under the MIT License.
// Full license text is available in 'licenses/MIT.txt'.
//
using System;
using System.Runtime.InteropServices;

using Mono.Unix.Native;
using Mono.Unix;


namespace Antmicro.Renode.Utilities
{
    public class LibCWrapper
    {
        public static int Open(string path, int mode)
        {
            var marshalledPath = Marshal.StringToHGlobalAnsi(path);
            var result = open(marshalledPath, mode);
            Marshal.FreeHGlobal(marshalledPath);
            return result;
        }

        public static int Close(int fd)
        {
            return close(fd);
        }

        public static bool Write(int fd, IntPtr buffer, int count)
        {
            var written = 0;
            while(written < count)
            {
                int writtenThisTime = write(fd, buffer + written, count - written);
                if(writtenThisTime <= 0)
                {
                    return false;
                }

                written += writtenThisTime;
            }

            return true;
        }

        public static byte[] Read(int fd, int count)
        {
            byte[] result = null;
            var buffer = Marshal.AllocHGlobal(count);
            var r = read(fd, buffer, count);
            if(r > 0)
            {
                result = new byte[r];
                Marshal.Copy(buffer, result, 0, r);
            }
            Marshal.FreeHGlobal(buffer);
            return result ?? new byte[0];
        }

        public static byte[] Read(int fd, int count, int timeout, Func<bool> shouldCancel)
        {
            int pollResult;
            var pollData = new Pollfd {
                fd = fd,
                events = PollEvents.POLLIN
            };

            do
            {
                pollResult = Syscall.poll(new [] { pollData }, timeout);
            }
            while(UnixMarshal.ShouldRetrySyscall(pollResult) && !shouldCancel());

            if(pollResult > 0)
            {
                return Read(fd, count);
            }
            else
            {
                return null;
            }
        }

        public static int Ioctl(int fd, int request, int arg)
        {
            return ioctl(fd, request, arg);
        }

        public static int Ioctl(int fd, int request, IntPtr arg)
        {
            return ioctl(fd, request, arg);
        }

        public static IntPtr Strcpy(IntPtr dst, IntPtr src)
        {
            return strcpy(dst, src);
        }

        public static string Strerror(int id)
        {
            return Marshal.PtrToStringAuto(strerror(id));
        }

        #region Externs

        [DllImport("libc", EntryPoint = "open", SetLastError = true)]
        private static extern int open(IntPtr pathname, int flags);

        [DllImport("libc", EntryPoint = "strcpy")]
        private static extern IntPtr strcpy(IntPtr dst, IntPtr src);

        [DllImport("libc", EntryPoint = "ioctl")]
        private static extern int ioctl(int d, int request, IntPtr a);

        [DllImport("libc", EntryPoint = "ioctl", SetLastError = true)]
        private static extern int ioctl(int d, int request, int a);

        [DllImport("libc", EntryPoint = "close")]
        private static extern int close(int fd);

        [DllImport("libc", EntryPoint = "write")]
        private static extern int write(int fd, IntPtr buf, int count);

        [DllImport("libc", EntryPoint = "read")]
        private static extern int read(int fd, IntPtr buf, int count);

        [DllImport("libc", EntryPoint = "strerror")]
        private static extern IntPtr strerror(int fd);

        #endregion
    }
}
