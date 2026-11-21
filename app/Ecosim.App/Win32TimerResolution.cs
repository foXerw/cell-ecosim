using System.Runtime.InteropServices;

namespace Ecosim.App;

/// <summary>
/// Asks Windows for 1 ms system timer resolution while the app runs.
///
/// Without it, every Task.Delay and DispatcherTimer lands on the default 15.6 ms
/// grid: a 16 ms render pump fires at 16 or 31 ms, and a 33 ms sim tick fires at
/// 31 or 47 ms. That irregularity is what read as a slideshow. The cost is a
/// slightly higher idle power draw while the window is open, which is the normal
/// trade for a desktop animation app.
/// </summary>
internal static partial class Win32TimerResolution
{
    [DllImport("winmm.dll")]
    private static extern uint timeBeginPeriod(uint uMilliseconds);

    [DllImport("winmm.dll")]
    private static extern uint timeEndPeriod(uint uMilliseconds);

    public static IDisposable UseOneMillisecond()
    {
        timeBeginPeriod(1);
        return new Scope();
    }

    private sealed class Scope : IDisposable
    {
        public void Dispose() => timeEndPeriod(1);
    }
}
