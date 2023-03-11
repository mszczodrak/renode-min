//
// Copyright (c) 2010-2023 Antmicro
// Copyright (c) 2011-2015 Realtime Embedded
//
// This file is licensed under the MIT License.
// Full license text is available in 'licenses/MIT.txt'.
//
using System;
using Antmicro.Renode.Logging;
using Antmicro.Renode.Logging.Backends;
using Antmicro.Renode.Core;
using Antmicro.Renode.UserInterface;
using Antmicro.Renode.Backends.Terminals;
using AntShell;
using AntShell.Terminal;
using Antmicro.Renode.Exceptions;
using Antmicro.Renode.Utilities;
using Antmicro.Renode.Peripherals.UART;
using System.Linq;
using System.IO;
using System.Diagnostics;
using Antmicro.Renode.Analyzers;

namespace Antmicro.Renode.UI
{
    public static class CommandLineInterface
    {
        public static void Run()
        {
            Console.Out.WriteLine("HELLO!");

            Console.Out.WriteLine("Console!");
            Logger.AddBackend(ConsoleBackend.Instance, "console");

            Logger.AddBackend(new MemoryBackend(), "memory");
            Emulator.ShowAnalyzers = true;

            using(var context = ObjectCreator.Instance.OpenContext())
            {
                var monitor = new Antmicro.Renode.UserInterface.Monitor();
                context.RegisterSurrogate(typeof(Antmicro.Renode.UserInterface.Monitor), monitor);

                Shell shell = null;

                Console.Out.WriteLine("Shell on port!");
                var io = new IOProvider()
                {
                    Backend = new SocketIOSource(1234)
                };
                shell = ShellProvider.GenerateShell(monitor, true);
                shell.Terminal = new NavigableTerminalEmulator(io, true);

                Logger.Log(LogLevel.Info, "Monitor available in telnet mode on port {0}", 1234);
                
                shell.Quitted += Emulator.Exit;

                monitor.Interaction = shell.Writer;
                monitor.MachineChanged += emu => shell.SetPrompt(emu != null ? new Prompt(string.Format("({0}) ", emu), ConsoleColor.DarkYellow) : null);

                shell.Terminal.PlainMode = false;

                new System.Threading.Thread(x => shell.Start(true))
                {
                    IsBackground = true,
                    Name = "Shell thread"
                }.Start();

                Emulator.BeforeExit += () =>
                {
                    Emulator.DisposeAll();
                };

                Console.Out.WriteLine("Wait!");
                Emulator.WaitForExit();
                Console.Out.WriteLine("Exit!");
            }
        }
    }
}
