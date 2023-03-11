using System.Threading;
using Antmicro.Renode;
using Antmicro.Renode.UI;
using System.IO;
using System;
using Antmicro.Renode.Utilities;
using Antmicro.Renode.RobotFramework;
using Antmicro.Renode.Logging;

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
                    Antmicro.Renode.UI.CommandLineInterface.Run();
                }
                finally
                {
                    Emulator.FinishExecutionAsMainThread();
                }
            });
            thread.Start();
            Emulator.ExecuteAsMainThread();
        }
    }
}
