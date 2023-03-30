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

            var sysbus = machine.SystemBus;
            //var cpus = sysbus.GetCPUs();
            //Console.Out.WriteLine("Number of CPUs is {0}", cpus.Count());

            // cpu PerformanceInMips 125
            foreach(var cpu in machine.SystemBus.GetCPUs().OfType<Antmicro.Renode.Peripherals.CPU.BaseCPU>())
            {
                cpu.PerformanceInMips = 125;
            }

            System.Console.WriteLine("ServerSocketTerminal");
            // emulation CreateServerSocketTerminal 3456 "term"
            var term = new Antmicro.Renode.Backends.Terminals.ServerSocketTerminal(3456, true, false);
            Antmicro.Renode.Core.EmulationManager.Instance.CurrentEmulation.ExternalsManager.AddExternal(term, "term");
            System.Console.WriteLine("ServerSocketTerminal Added");

            // connector Connect sysbus.uart4 term
            IEmulationElement uartEmulationElement = null;
            EmulationManager.Instance.CurrentEmulation.TryGetEmulationElementByName("uart4", machine, out uartEmulationElement);
            Antmicro.Renode.Core.EmulationManager.Instance.CurrentEmulation.Connector.Connect(uartEmulationElement, term);

            //IEmulationElement termEmulationElement = null;
            //EmulationManager.Instance.CurrentEmulation.TryGetEmulationElementByName("term", machine, out termEmulationElement);

            //System.Console.WriteLine("Print Names");
            //var names = Antmicro.Renode.Core.EmulationManager.Instance.CurrentEmulation.ExternalsManager.GetNames();
            //foreach(var name in names)
            //{
            //    System.Console.WriteLine(name);
            //}
            //System.Console.WriteLine("Print Names Done");
            //Antmicro.Renode.Backends.Terminals.ServerSocketTerminal external;
            //if(EmulationManager.Instance.CurrentEmulation.ExternalsManager.TryGetByName("term", out external))
            //{
            //}


            // sysbus LoadELF @https://dl.antmicro.com/projects/renode/stm32f4discovery.elf-s_445441-827a0dedd3790f4559d7518320006613768b5e72
        }        
    }
}
