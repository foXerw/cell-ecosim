using Avalonia;

namespace Ecosim.App;

internal static class Program
{
    // Avalonia's entry point contract: STAThread + BuildAvaloniaApp is also what
    // the previewer uses.
    [STAThread]
    public static void Main(string[] args)
    {
        using var _ = Win32TimerResolution.UseOneMillisecond();
        BuildAvaloniaApp().StartWithClassicDesktopLifetime(args);
    }

    public static AppBuilder BuildAvaloniaApp()
        => AppBuilder.Configure<App>()
            .UsePlatformDetect()
            .LogToTrace();
}
