using System.Text;

namespace PowerMenuUiaProbe;

internal static class Program
{
    public static int Main(string[] args)
    {
        Console.OutputEncoding = new UTF8Encoding(encoderShouldEmitUTF8Identifier: false);

        if (args.Contains("--help", StringComparer.OrdinalIgnoreCase))
        {
            Console.WriteLine("PowerMenuUiaProbe [--duration-seconds 5..120] [--sample-interval-ms 100..2000]");
            Console.WriteLine("Default: 30 seconds, 500 ms interval; reads the live cursor, keyboard focus, and related Control/Raw View ancestry without input.");
            return 0;
        }

        return InteractivePowerMenuMonitor.Run(args);
    }
}
