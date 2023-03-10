//
// Copyright (c) 2010-2021 Antmicro
// Copyright (c) 2011-2015 Realtime Embedded
//
// This file is licensed under the MIT License.
// Full license text is available in 'licenses/MIT.txt'.
//
using Antmicro.OptionsParser;

namespace Antmicro.Renode.UI
{
    public class Options : IValidatedOptions
    {
        [Name('p', "plain"), DefaultValue(false), Description("Remove steering codes (e.g., colours) from output.")]
        public bool Plain { get; set; }

        [Name('P', "port"), DefaultValue(-1), Description("Instead of opening a window, listen for Monitor commands on the specified port.")]
        public int Port { get; set; }

        [Name('e', "execute"), Description("Execute command on startup (executed after the optional script). May be used many times.")]
        public string[] Execute { get; set; }

        [Name("config"), Description("Use the configuration file from the provided path, or create one if it does not exist")]
        public string ConfigFile { get; set; }

        [Name("script"), PositionalArgument(0)]
        public string ScriptPath { get; set; }

        [Name("hide-log"), DefaultValue(false), Description("Do not show log messages in a console.")]
        public bool HideLog { get; set; }

        [Name("hide-analyzers"), DefaultValue(false), Description("Do not show analyzers.")]
        public bool HideAnalyzers { get; set; }

        [Name("robot-server-port"), DefaultValue(-1), Description("Start robot framework remote server on the specified port.")]
        public int RobotFrameworkRemoteServerPort { get; set; }

        [Name('v', "version"), DefaultValue(false), Description("Print version and exit.")]
        public bool Version { get; set; }

        [Name("console"), Description("Run the Monitor in the console instead of a separate window")]
        public bool Console { get; set; }

        [Name("keep-temporary-files"), Description("Don't clean temporary files on exit")]
        public bool KeepTemporaryFiles { get; set; }

        public bool Validate(out string error)
        {

            error = null;
            return true;
        }
    }
}

