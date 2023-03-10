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
using Antmicro.OptionsParser;
using System.IO;
using System.Diagnostics;
using Antmicro.Renode.Analyzers;

namespace Antmicro.Renode.UI
{
    public static class CommandLineInterface
    {
        public static void Run(Options options, Action<ObjectCreator.Context> beforeRun = null)
        {
            //AppDomain.CurrentDomain.UnhandledException += (sender, e) => CrashHandler.HandleCrash((Exception)e.ExceptionObject);

            if(options.Version)
            {
                Console.Out.WriteLine(EmulationManager.Instance.VersionString);
                return;
            }

            if(options.KeepTemporaryFiles)
            {
                EmulationManager.DisableEmulationFilesCleanup = true;
            }

            if(!options.HideLog)
            {
                Logger.AddBackend(ConsoleBackend.Instance, "console");
                if(options.Plain)
                {
                    //This is set in Program.cs already, but we leave it here in case CommandLineInterface is reused,
                    //to prevent hard to trace bugs
                    ConsoleBackend.Instance.PlainMode = true;
                }
            }
            else
            {
                Logger.AddBackend(new DummyLoggerBackend(), "dummy");
            }

            Logger.AddBackend(new MemoryBackend(), "memory");
            Emulator.ShowAnalyzers = !options.HideAnalyzers;
            if(options.PidFile != null)
            {
                var pid = Process.GetCurrentProcess().Id;
                File.WriteAllText(options.PidFile, pid.ToString());
            }

            options.Port = 1234;


            using(var context = ObjectCreator.Instance.OpenContext())
            {
                var monitor = new Antmicro.Renode.UserInterface.Monitor();
                context.RegisterSurrogate(typeof(Antmicro.Renode.UserInterface.Monitor), monitor);

                // we must initialize plugins AFTER registering monitor surrogate
                // as some plugins might need it for construction
                TypeManager.Instance.PluginManager.Init("CLI");

                EmulationManager.Instance.ProgressMonitor.Handler = new CLIProgressMonitor();

                var uartAnalyzerType = (options.RobotDebug) ? typeof(LoggingUartAnalyzer) : typeof(ConsoleWindowBackendAnalyzer);

                EmulationManager.Instance.CurrentEmulation.BackendManager.SetPreferredAnalyzer(typeof(UARTBackend), uartAnalyzerType);
                EmulationManager.Instance.EmulationChanged += () =>
                {
                    EmulationManager.Instance.CurrentEmulation.BackendManager.SetPreferredAnalyzer(typeof(UARTBackend), uartAnalyzerType);
                };

                var shell = PrepareShell(options, monitor);
                new System.Threading.Thread(x => shell.Start(true))
                {
                    IsBackground = true,
                    Name = "Shell thread"
                }.Start();

                Emulator.BeforeExit += () =>
                {
                    Emulator.DisposeAll();
                };

                if(beforeRun != null)
                {
                    beforeRun(context);
                }

                if(options.RobotDebug)
                {
                    ConsoleWindowBackendAnalyzer terminal = null;
                }

                Emulator.WaitForExit();
            }
        }

        private static Shell PrepareShell(Options options, Monitor monitor)
        {
            Shell shell = null;
            if(options.Console)
            {
                var io = new IOProvider()
                {
                    Backend = new ConsoleIOSource()
                };
                shell = ShellProvider.GenerateShell(monitor, true);
                shell.Terminal = new NavigableTerminalEmulator(io, true);
            }
            else if(options.Port >= 0)
            {
                var io = new IOProvider()
                {
                    Backend = new SocketIOSource(options.Port)
                };
                shell = ShellProvider.GenerateShell(monitor, true);
                shell.Terminal = new NavigableTerminalEmulator(io, true);

                Logger.Log(LogLevel.Info, "Monitor available in telnet mode on port {0}", options.Port);
            }

            shell.Quitted += Emulator.Exit;

            monitor.Interaction = shell.Writer;
            monitor.MachineChanged += emu => shell.SetPrompt(emu != null ? new Prompt(string.Format("({0}) ", emu), ConsoleColor.DarkYellow) : null);

            if(!string.IsNullOrEmpty(options.ScriptPath))
            {
                shell.Started += s => s.InjectInput(string.Format("i {0}{1}\n", Path.IsPathRooted(options.ScriptPath) ? "@" : "$CWD/", options.ScriptPath));
            }
            if(options.Execute != null)
            {
                shell.Started += s => s.InjectInput(string.Format("{0}\n", string.Join("; ", options.Execute)));
            }

            shell.Terminal.PlainMode = options.Plain;

            return shell;
        }
    }
}
