using System.Threading;
using System;
using Antmicro.Renode.Utilities;
using Antmicro.Renode.Logging;
using Antmicro.Renode.Logging.Backends;
using Antmicro.Renode.Core;
using Antmicro.Renode.UserInterface;
using AntShell;
using AntShell.Terminal;

namespace Antmicro.Renode
{
    public class Program
    {
        [STAThread]
        public static void Main(string[] args)
        {
            Console.Out.WriteLine("Main!");
            AppDomain.CurrentDomain.ProcessExit += (_, __) => Emulator.Exit();

            Core.EmulationManager.RebuildInstance();

            var thread = new Thread(() =>
            {
                try
                {
                    RunMe();
                }
                finally
                {
                    Emulator.FinishExecutionAsMainThread();
                }
            });
            thread.Start();
            Emulator.ExecuteAsMainThread();
        }


        public static void RunMe()
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
