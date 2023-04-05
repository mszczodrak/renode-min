using System.Threading;
using System;
using System.Linq;
using Antmicro.Renode.Logging;
using AntShell;
using AntShell.Terminal;

namespace Antmicro.Renode
{
    public class Program
    {
        [STAThread]
        public static void Main(string[] args)
        {
            AppDomain.CurrentDomain.ProcessExit += (_, __) => Emulator.Exit();
            Core.EmulationManager.RebuildInstance();
            Emulator.BeforeExit += () =>
            {
                Emulator.DisposeAll();
            };
            
            RunShell();
            //RunDirect();
        }


        public static void RunDirect() {
            System.Console.Out.WriteLine("STUDIO [Program] Hello Direct");

            Logger.AddBackend(ConsoleBackend.Instance, "console");
            var context = Core.ObjectCreator.Instance.OpenContext();
            var monitor = new Antmicro.Renode.UserInterface.Monitor();
            context.RegisterSurrogate(typeof(Antmicro.Renode.UserInterface.Monitor), monitor);

            var dummyWriter = new Antmicro.Renode.UserInterface.DummyCommandInteraction(true);
            monitor.Interaction = dummyWriter;

            monitor.HandleCommand("mach create \"STM32F4_Flat\"", null);
            monitor.HandleCommand("machine LoadPlatformDescription @platforms/boards/stm32f4_flat.repl", null);
            monitor.HandleCommand("sysbus.cpu PerformanceInMips 125", null);
            monitor.HandleCommand("emulation CreateServerSocketTerminal 3456 \"term\"", null);
            monitor.HandleCommand("connector Connect sysbus.uart4 term", null);
            monitor.HandleCommand("sysbus LoadELF @https://dl.antmicro.com/projects/renode/stm32f4discovery.elf-s_445441-827a0dedd3790f4559d7518320006613768b5e72", null);
            monitor.HandleCommand("start", null);

            Thread.Sleep(4000);
        }


        public static void RunShell()
        {
            System.Console.Out.WriteLine("STUDIO [Program] Hello Shell");

            Logger.AddBackend(ConsoleBackend.Instance, "console");
            var context = Core.ObjectCreator.Instance.OpenContext();
            var monitor = new Antmicro.Renode.UserInterface.Monitor();
            context.RegisterSurrogate(typeof(Antmicro.Renode.UserInterface.Monitor), monitor);

            Shell shell = null;

            System.Console.Out.WriteLine("STUDIO [Program] Shell on port");
            var io = new IOProvider()
            {
                Backend = new Utilities.SocketIOSource(1234)
            };
            shell = UserInterface.ShellProvider.GenerateShell(monitor, true);
            shell.Terminal = new NavigableTerminalEmulator(io, true);

            Logger.Log(LogLevel.Info, "Monitor available in telnet mode on port {0}", 1234);
            
            shell.Quitted += Emulator.Exit;
            monitor.Interaction = shell.Writer;

            new System.Threading.Thread(x => shell.Start(true))
            {
                IsBackground = true,
                Name = "Shell thread"
            }.Start();

            System.Console.Out.WriteLine("STUDIO [Program] Wait");
            Emulator.WaitForExit();
            System.Console.Out.WriteLine("STUDIO [Program] Exit");
            
        }
        
    }
}
