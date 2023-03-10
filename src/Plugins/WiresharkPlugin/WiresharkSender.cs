﻿/**************************************************************************
*                           MIT License
*
* Copyright (C) 2015 Frederic Chaxel <fchaxel@free.fr>
* 
* Copyright (c) Antmicro
*
* Permission is hereby granted, free of charge, to any person obtaining
* a copy of this software and associated documentation files (the
* "Software"), to deal in the Software without restriction, including
* without limitation the rights to use, copy, modify, merge, publish,
* distribute, sublicense, and/or sell copies of the Software, and to
* permit persons to whom the Software is furnished to do so, subject to
* the following conditions:
*
* The above copyright notice and this permission notice shall be included
* in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*
*********************************************************************/
using System;
using System.Runtime.InteropServices;
using System.IO;
using System.Diagnostics;
using System.IO.Pipes;
using Antmicro.Renode.Utilities;

namespace Antmicro.Renode.Plugins.WiresharkPlugin
{
    public class WiresharkSender
    {
        public WiresharkSender(string pipeName, uint pcapNetId, string wiresharkPath)
        {
            this.pipeName = pipeName;
            this.pcapNetId = pcapNetId;
            this.wiresharkPath = wiresharkPath;
        }

        public void ClearPipe()
        {
            if(wiresharkPipe != null)
            {
                wiresharkPipe.Close();
                //As named pipes on Linux have their entries in the filesystem, we remove it as a cleanup.
                File.Delete(pipeName);
            }
        }

        public bool TryOpenWireshark()
        {
            if(isConnected)
            {
                return false;
            }
            lastReportedFrame = null;

            // Mono is using the "/var/tmp" prefix for pipes by default.
            // Because of problems with setting GID bit on OSX in that directory, we combine this default path with an absolute EmulatorTemporaryPath value, which effectively overwrites the default - Path.Combine of two absolute paths drops the first one.
            wiresharkPipe = new NamedPipeServerStream(GetPrefixedPipeName(), PipeDirection.Out, 1, PipeTransmissionMode.Byte, NamedPipeOptions);
            wiresharkProces = new Process();
            wiresharkProces.EnableRaisingEvents = true;

            wiresharkProces.StartInfo = new ProcessStartInfo(wiresharkPath, $"-ni {GetPrefixedPipeName()} -k")
            {
                UseShellExecute = false,
                RedirectStandardError = true,
                RedirectStandardOutput = true,
                RedirectStandardInput = true
            };
            wiresharkProces.Exited += (sender, e) =>
            {
                isConnected = false;
                ClearPipe();
            };

            wiresharkProces.Start();
            wiresharkPipe.WaitForConnection();
            isConnected = true;
            SendWiresharkGlobalHeader();

            return true;
        }

        public void CloseWireshark()
        {
            if(wiresharkProces == null)
            {
                return;
            }

            try
            {
                if(!wiresharkProces.HasExited)
                {
                    wiresharkProces.CloseMainWindow();
                }
            }
            catch(InvalidOperationException e)
            {
                // do not report an exception if the program has already exited
                if(!e.Message.Contains("finished"))
                {
                    throw;
                }
            }
            wiresharkProces = null;
        }

        public void SendReportedFrames(byte[] buffer)
        {
            if(lastReportedFrame != buffer)
            {
                SendToWireshark(buffer, 0, buffer.Length);
                lastReportedFrame = buffer;
            }
        }

        public void SendProcessedFrames(byte[] buffer)
        {
            if(lastProcessedFrame != buffer)
            {
                SendToWireshark(buffer, 0, buffer.Length);
                lastProcessedFrame = buffer;
            }
        }

        private string GetPrefixedPipeName()
        {
            // Mono is using the "/var/tmp" prefix for pipes by default.
            // Because of problems with setting GID bit on OSX in that directory, we combine this default path with an absolute EmulatorTemporaryPath value, which effectively overwrites the default - Path.Combine of two absolute paths drops the first one.
            return $"{namedPipePrefix}{pipeName}";
        }

        private void SendWiresharkGlobalHeader()
        {
            var p = new PcapGlobalHeader(pcapNetId);
            var bh = p.ToByteArray();
            wiresharkPipe.Write(bh, 0, bh.Length);
        }

        private bool SendToWireshark(byte[] buffer, int offset, int lenght)
        {
            return SendToWireshark(buffer, offset, lenght, CustomDateTime.Now);
        }

        private bool SendToWireshark(byte[] buffer, int offset, int lenght, DateTime date)
        {
            // Suppress all values for ms, us and ns
            var roundedDate = new DateTime((date.Ticks / (long)10000000) * (long)10000000);

            var seconds = DateTimeToUnixTimestamp(date);
            var microseconds = (UInt32)((date.Ticks - roundedDate.Ticks) / 10);

            return SendToWireshark(buffer, offset, lenght, seconds, microseconds);
        }

        private bool SendToWireshark(byte[] buffer, int offset, int lenght, uint seconds, uint microseconds)
        {
            if(!isConnected)
            {
                return false;
            }

            var header = new PcapPacketHeader((uint)lenght, seconds, microseconds);
            var headerBytes = header.ToByteArray();

            try
            {
                // Wireshark Header
                wiresharkPipe.Write(headerBytes, 0, headerBytes.Length);

                // Bacnet packet
                wiresharkPipe.Write(buffer, offset, lenght);

            }
            catch(Exception)
            {
                // We should probably handle IOException to somehow restart the pipe.
                // It is difficult to test, though, and Wireshark may not survive it.
                return false;
            }

            return true;
        }

        private static uint DateTimeToUnixTimestamp(DateTime dateTime)
        {
            return (uint)(dateTime - localEpoch).TotalSeconds;
        }

        private NamedPipeServerStream wiresharkPipe;
        private Process wiresharkProces;
        private string pipeName;
        private uint pcapNetId;
        private string wiresharkPath;
        private bool isConnected;
        private byte[] lastReportedFrame;
        private byte[] lastProcessedFrame;

        private static readonly DateTime localEpoch = Misc.UnixEpoch.ToLocalTime();

        private string namedPipePrefix = Utilities.TemporaryFilesManager.Instance.EmulatorTemporaryPath;
        private const PipeOptions NamedPipeOptions = PipeOptions.None;


        [StructLayout(LayoutKind.Sequential, Pack = 1)]
        struct PcapPacketHeader
        {
            public PcapPacketHeader(uint lenght, uint seconds, uint microseconds)
            {
                savedBytesLength = packetLength = lenght;
                timestampSeconds = seconds;
                timestampMicroseconds = microseconds;
            }

            public byte[] ToByteArray()
            {
                var rawsize = Marshal.SizeOf(this);
                var rawdatas = new byte[rawsize];
                var handle = GCHandle.Alloc(rawdatas, GCHandleType.Pinned);
                var buffer = handle.AddrOfPinnedObject();
                Marshal.StructureToPtr(this, buffer, false);
                handle.Free();
                return rawdatas;
            }

            /* timestamp seconds */
            private uint timestampSeconds;
            /* timestamp microseconds */
            private uint timestampMicroseconds;
            /* number of octets of packet saved in file */
            private uint savedBytesLength;
            /* actual length of packet */
            private uint packetLength;
        }

        [StructLayout(LayoutKind.Sequential, Pack = 1)]
        struct PcapGlobalHeader
        {
            public PcapGlobalHeader(uint network)
            {
                magicNumber = 0xa1b2c3d4;
                majorVersion = 2;
                minorVersion = 4;
                timezoneCorrection = 0;
                sigfigs = 0;
                maximumPacketLength = 65535;
                networkType = network;
            }

            public byte[] ToByteArray()
            {
                var rawsize = Marshal.SizeOf(this);
                var rawdata = new byte[rawsize];
                var handle = GCHandle.Alloc(rawdata, GCHandleType.Pinned);
                var buffer = handle.AddrOfPinnedObject();
                Marshal.StructureToPtr(this, buffer, false);
                handle.Free();
                return rawdata;
            }

            /* magic number */
            private uint magicNumber;
            /* major version number */
            private ushort majorVersion;
            /* minor version number */
            private ushort minorVersion;
            /* GMT to local correction */
            private int timezoneCorrection;
            /* accuracy of timestamps */
            private uint sigfigs;
            /* max length of captured packets, in octets */
            private uint maximumPacketLength;
            /* data link type */
            private uint networkType;
        }
    }
}
