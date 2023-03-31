using System.Threading;
using System;
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
            
            // RunShell();
            RunDirect();
        }


        public static void RunDirect() {

            System.Console.Out.WriteLine("STUDIO [Program] Hello Direct");
            Logger.AddBackend(ConsoleBackend.Instance, "console");

            var monitor = new UserInterface.Monitor();
            var context = Core.ObjectCreator.Instance.OpenContext();
            context.RegisterSurrogate(typeof(UserInterface.Monitor), monitor);


            // mach create "STM32F4_Discovery"
            Core.Machine machine;
            machine = new Core.Machine();
            Core.EmulationManager.Instance.CurrentEmulation.AddMachine(machine, "STM32F4_flat");




            // machine LoadPlatformDescription @platforms/boards/stm32f4_discovery-kit.repl
            //var usingResolver = new Antmicro.Renode.PlatformDescription.UserInterface.PlatformDescriptionMachineExtensions.UsingResolver(monitor.CurrentPathPrefixes);
            //var monitorInitHandler = new Antmicro.Renode.PlatformDescription.UserInterface.MonitorInitHandler(machine, monitor);
            //var driver = new Antmicro.Renode.PlatformDescription.CreationDriver(machine, usingResolver, monitorInitHandler);
            //driver.ProcessFile("/home/marcin/src/renode-min/platforms/boards/stm32f4_flat.repl");

            //var sysbus = machine.SystemBus;
            //var cpus = sysbus.GetCPUs();
            //System.Console.Out.WriteLine("STUDIO [Program] Number of CPUs is {0}", cpus.Count());

            // cpu PerformanceInMips 125
            // see CpuKeyword.cs file
            //var cpu = sysbus.GetCPUId(0)  as Antmicro.Renode.Peripherals.CPU.BaseCPU;
            //cpu.PerformanceInMips = 125;
        }


        public static void RunShell()
        {
            System.Console.Out.WriteLine("STUDIO [Program] Hello Shell");
            Logger.AddBackend(ConsoleBackend.Instance, "console");

            using(var context = Core.ObjectCreator.Instance.OpenContext())
            {
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
                monitor.MachineChanged += emu => shell.SetPrompt(emu != null ? new Prompt(string.Format("({0}) ", emu), ConsoleColor.DarkYellow) : null);

                shell.Terminal.PlainMode = false;

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
}
