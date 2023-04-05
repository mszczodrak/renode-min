using System.Threading;
using System;
using System.Linq;
using Antmicro.Renode.Logging;
//using AntShell;
//using AntShell.Terminal;

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
            System.Console.Out.WriteLine("STUDIO [Program] Hello Direct");

            Logger.AddBackend(ConsoleBackend.Instance, "console");
            var context = Core.ObjectCreator.Instance.OpenContext();
            var monitor = new Antmicro.Renode.UserInterface.Monitor();
            context.RegisterSurrogate(typeof(Antmicro.Renode.UserInterface.Monitor), monitor);

            var dummyWriter = new Antmicro.Renode.UserInterface.DummyCommandInteraction(true);
            monitor.Interaction = dummyWriter;

            //monitor.HandleCommand("mach create \"STM32F4_Flat\"", null);
            //var machine = new Core.Machine();
            //Core.EmulationManager.Instance.CurrentEmulation.AddMachine(machine, "ST");

            //monitor.HandleCommand("machine LoadPlatformDescription @platforms/boards/stm32f4_flat.repl", null);
            //var driver = PlatformDescription.UserInterface.PlatformDescriptionMachineExtensions.PrepareDriver(machine);
            //driver.ProcessFile("/home/marcin/src/renode-min/platforms/boards/stm32f4_flat.repl");


            //monitor.HandleCommand("sysbus.cpu PerformanceInMips 125", null);


            
            monitor.HandleCommand("mach create \"STM32F4_Flat\"", null);
            monitor.HandleCommand("machine LoadPlatformDescription @platforms/boards/stm32f4_flat.repl", null);
            monitor.HandleCommand("sysbus.cpu PerformanceInMips 125", null);
            monitor.HandleCommand("emulation CreateServerSocketTerminal 3456 \"term\"", null);
            monitor.HandleCommand("connector Connect sysbus.uart4 term", null);
            monitor.HandleCommand("sysbus LoadELF @https://dl.antmicro.com/projects/renode/stm32f4discovery.elf-s_445441-827a0dedd3790f4559d7518320006613768b5e72", null);
            monitor.HandleCommand("start", null);
            

            Thread.Sleep(4000);
        }
        
    }
}
