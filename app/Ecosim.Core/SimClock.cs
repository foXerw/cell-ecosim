using System.Diagnostics;

namespace Ecosim.Core;

public sealed class TickEventArgs : EventArgs
{
    public TickEventArgs(long generation, int aliveCount, double stepMs, int generationsRun)
    {
        Generation = generation;
        AliveCount = aliveCount;
        StepMs = stepMs;
        GenerationsRun = generationsRun;
    }

    public long Generation { get; }

    /// <summary>Per-species counts arrive with milestone M1 task 10; -1 until then.</summary>
    public int AliveCount { get; }

    /// <summary>Wall time of the step that just ran, so the UI can show real cost.</summary>
    public double StepMs { get; }

    public int GenerationsRun { get; }
}

/// <summary>
/// Drives the simulation at a target rate. The clock lives on the managed side
/// on purpose: native never spawns a stepping thread, so by construction only
/// this loop ever calls ecosim_world_step (design appendix A.2).
/// </summary>
public sealed class SimClock : IDisposable
{
    private const int MaxCatchUpPerTick = 16;

    private readonly World _world;
    private readonly object _sync = new();
    private CancellationTokenSource? _cts;
    private Task? _loop;
    private SynchronizationContext? _context;
    private int _targetGenPerSec;
    private bool _running;
    private bool _disposed;

    /// <summary>Raised after each batch of steps, on the SynchronizationContext
    /// captured by <see cref="Start"/> (the UI thread in the app), otherwise on
    /// the clock thread.</summary>
    public event EventHandler<TickEventArgs>? Tick;

    public SimClock(World world, int targetGenPerSec)
    {
        ArgumentNullException.ThrowIfNull(world);
        if (targetGenPerSec < 1)
        {
            throw new ArgumentOutOfRangeException(nameof(targetGenPerSec), "速度必须至少为 1 代/秒");
        }
        _world = world;
        _targetGenPerSec = targetGenPerSec;
    }

    public int TargetGenPerSec => Volatile.Read(ref _targetGenPerSec);

    /// <summary>Whether the clock intends to be stepping. Set synchronously by
    /// Start/Stop, so a pause is observable immediately even while the loop task
    /// is still unwinding.</summary>
    public bool IsRunning
    {
        get
        {
            lock (_sync) return _running;
        }
    }

    /// <summary>Set if the loop gave up because the engine reported an error;
    /// the clock stops rather than throwing on a background thread.</summary>
    public Exception? Failure { get; private set; }

    /// <summary>
    /// The most recent immutable copy of the board, captured right after each
    /// step. The render pump paints from this instead of reaching into the
    /// engine, so painting and stepping never wait on each other.
    /// </summary>
    public SpeciesSnapshot? LatestSnapshot { get; private set; }

    public void Start()
    {
        lock (_sync)
        {
            if (_disposed) throw new ObjectDisposedException(nameof(SimClock));
            if (_running) return;

            _context = SynchronizationContext.Current;
            var cts = new CancellationTokenSource();
            _cts = cts;
            _running = true;
            _loop = Task.Run(() => RunAsync(cts.Token));
            var loop = _loop;

            // The loop clears its own registration when it finishes, so a Stop
            // (which is synchronous) cannot leave a stale task behind.
            _ = loop.ContinueWith(_ =>
            {
                lock (_sync)
                {
                    if (ReferenceEquals(_loop, loop))
                    {
                        _loop = null;
                        if (ReferenceEquals(_cts, cts))
                        {
                            _cts = null;
                            cts.Dispose();
                        }
                    }
                }
            }, CancellationToken.None, TaskContinuationOptions.ExecuteSynchronously,
               TaskScheduler.Default);
        }
    }

    public void SetSpeed(int genPerSec)
    {
        if (genPerSec < 1)
        {
            throw new ArgumentOutOfRangeException(nameof(genPerSec), "速度必须至少为 1 代/秒");
        }
        Volatile.Write(ref _targetGenPerSec, genPerSec);
    }

    public void Stop()
    {
        CancellationTokenSource? cts;
        lock (_sync)
        {
            _running = false;
            cts = _cts;
        }
        cts?.Cancel();
    }

    public async Task StopAsync()
    {
        Task? loop;
        CancellationTokenSource? cts;
        lock (_sync)
        {
            _running = false;
            loop = _loop;
            cts = _cts;
        }
        cts?.Cancel();
        if (loop is null) return;

        try
        {
            await loop.ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
            // the loop exits by cancellation; that is the normal ending
        }
    }

    public void Dispose()
    {
        lock (_sync)
        {
            if (_disposed) return;
            _disposed = true;
            _running = false;
        }
        // Only cancel here: disposing the source while the loop is parked on
        // Task.Delay would turn a clean cancellation into an ObjectDisposedException.
        _cts?.Cancel();
    }

    /// <summary>
    /// How many generations to run this tick, and what to carry into the next.
    ///
    /// The carry is the point: rounding elapsed*target to an integer throws away
    /// a fraction of a generation every tick, so a "30 gen/s" clock drifts down
    /// to whatever the OS timer grid allows. Accumulating keeps the long-run rate
    /// exact. Pure function, so the maths is testable without wall-clock timing.
    /// </summary>
    public static (int Steps, double Carry) BudgetFor(double elapsedSeconds, int targetGenPerSec,
                                                      double carry)
    {
        if (elapsedSeconds < 0) elapsedSeconds = 0;
        var total = carry + elapsedSeconds * Math.Max(targetGenPerSec, 1);
        var steps = Math.Min((int)total, MaxCatchUpPerTick);
        // A slow tick costs a skipped few generations, never a growing backlog:
        // anything beyond what we can run now is dropped, not carried.
        return (Math.Max(steps, 0), Math.Clamp(total - steps, 0.0, 1.0));
    }

    private async Task RunAsync(CancellationToken token)
    {
        var previous = Stopwatch.GetTimestamp();
        var carry = 0.0;

        while (!token.IsCancellationRequested)
        {
            var period = TimeSpan.FromSeconds(1.0 / TargetGenPerSec);
            try
            {
                await Task.Delay(period, token).ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
                break;
            }

            var now = Stopwatch.GetTimestamp();
            var elapsedSeconds = (now - previous) / (double)Stopwatch.Frequency;
            previous = now;

            var (due, nextCarry) = BudgetFor(elapsedSeconds, TargetGenPerSec, carry);
            carry = nextCarry;
            if (due <= 0) continue;

            var sw = Stopwatch.StartNew();
            try
            {
                _world.Step(due);
                LatestSnapshot = _world.Capture();
            }
            catch (Exception ex)
            {
                lock (_sync)
                {
                    Failure = ex;
                    _running = false;
                }
                break;
            }
            sw.Stop();

            Raise(new TickEventArgs(_world.Generation, aliveCount: -1,
                                    sw.Elapsed.TotalMilliseconds, due));
        }
    }

    private void Raise(TickEventArgs args)
    {
        var handlers = Tick;
        if (handlers is null) return;

        var context = _context;
        if (context is null)
        {
            handlers(this, args);
            return;
        }
        context.Post(_ => handlers(this, args), null);
    }
}
