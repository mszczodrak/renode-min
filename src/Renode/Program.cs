using System.Threading;
using System;
using Antmicro.Renode.Utilities;
using Antmicro.Renode.Logging;
using Antmicro.Renode.Logging.Backends;
using Antmicro.Renode.Core;
using Antmicro.Renode.UserInterface;
using AntShell;
using AntShell.Terminal;
using System.Linq;

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
            
            //RunShell();
            RunDirect();
        }


        public static void RunDirect() {
            var monitor = new Antmicro.Renode.UserInterface.Monitor();
            
            // mach create "STM32F4_Discovery"
            var machine = new Machine();
            Antmicro.Renode.Core.EmulationManager.Instance.CurrentEmulation.AddMachine(machine, "STM32F4_Discovery");

            // machine LoadPlatformDescription @platforms/boards/stm32f4_discovery-kit.repl
            var usingResolver = new Antmicro.Renode.PlatformDescription.UserInterface.PlatformDescriptionMachineExtensions.UsingResolver(monitor.CurrentPathPrefixes);
            var monitorInitHandler = new Antmicro.Renode.PlatformDescription.FakeInitHandler();
            var driver = new Antmicro.Renode.PlatformDescription.CreationDriver(machine, usingResolver, monitorInitHandler);
            driver.ProcessFile("/home/marcin/src/renode-min/platforms/boards/stm32f4_discovery-kit.repl");

            //var sysbus = machine.SystemBus;
            //var cpus = sysbus.GetCPUs();
            //Console.Out.WriteLine("Number of CPUs is {0}", cpus.Count());

            // cpu PerformanceInMips 125
            foreach(var cpu in machine.SystemBus.GetCPUs().OfType<Antmicro.Renode.Peripherals.CPU.BaseCPU>())
            {
                cpu.PerformanceInMips = 125;
            }

            // emulation CreateServerSocketTerminal 3456 "term"


            // connector Connect sysbus.uart4 term

            // sysbus LoadELF @https://dl.antmicro.com/projects/renode/stm32f4discovery.elf-s_445441-827a0dedd3790f4559d7518320006613768b5e72
        }


        public static void RunShell()
        {
            Console.Out.WriteLine("HELLO!");
            Logger.AddBackend(ConsoleBackend.Instance, "console");

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

                Console.Out.WriteLine("Wait!");
                Emulator.WaitForExit();
                Console.Out.WriteLine("Exit!");
            }
        }

        
    }
}
