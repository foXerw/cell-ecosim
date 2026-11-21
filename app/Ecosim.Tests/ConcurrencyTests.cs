using Ecosim.Core;

namespace Ecosim.Tests;

/// <summary>
/// Review Focus #5: the engine hands out borrowed pointers and swaps buffers
/// while stepping, so every native call must go through the world's single
/// lock. These tests hammer the three paths that touch native memory at once.
/// </summary>
public class ConcurrencyTests
{
    [Fact]
    public void Concurrent_paint_step_and_render_never_throw_and_stay_consistent()
    {
        using var world = new World(new WorldConfig { Width = 128, Height = 128, Threads = 4 });
        world.Randomize(new SeedConfig { Species = 1, DensityPermille = 200, Seed = 31 });

        var buffer = new FrameBuffer();
        var exceptions = new System.Collections.Concurrent.ConcurrentQueue<Exception>();

        var tasks = new[]
        {
            Task.Run(() =>
            {
                try { for (var i = 0; i < 300; ++i) world.Step(1); }
                catch (Exception ex) { exceptions.Enqueue(ex); }
            }),
            Task.Run(() =>
            {
                try
                {
                    for (var i = 0; i < 300; ++i)
                    {
                        world.Paint(new[] { new PaintOp(i % 100, i % 100, i % 100 + 3, i % 100 + 3, 1) });
                    }
                }
                catch (Exception ex) { exceptions.Enqueue(ex); }
            }),
            Task.Run(() =>
            {
                try
                {
                    for (var i = 0; i < 300; ++i) world.RenderFrame(buffer, Palette.Default);
                }
                catch (Exception ex) { exceptions.Enqueue(ex); }
            }),
            Task.Run(() =>
            {
                try { for (var i = 0; i < 300; ++i) _ = world.Stats(); }
                catch (Exception ex) { exceptions.Enqueue(ex); }
            }),
        };

        Task.WaitAll(tasks, TimeSpan.FromSeconds(30));
        Assert.Empty(exceptions);

        // The frame must still be a coherent render of *some* generation:
        // every pixel is one of the palette colours, never torn bytes.
        world.RenderFrame(buffer, Palette.Default);
        var allowed = new HashSet<uint>
        {
            Palette.Default[0], Palette.Default[1], Palette.Default[2], Palette.Default[3],
        };
        for (var i = 0; i < buffer.Bytes.Length; i += 4)
        {
            Assert.Contains(BitConverter.ToUInt32(buffer.Bytes, i), allowed);
        }
    }

    [Fact]
    public void Disposing_a_world_while_its_clock_steps_does_not_corrupt_memory()
    {
        // Runs the interleaving that originally crashed the test host: a clock
        // still stepping while its world is disposed underneath it. This does
        // not by itself prove the use-after-free is gone - freed memory that
        // nobody has reused yet still behaves, which is why the crash was
        // intermittent - so the deterministic guard for it lives in
        // Disposing_waits_for_an_in_flight_native_call below. What this one does
        // pin is the contract callers can see: the outcome is a clean stop or an
        // ObjectDisposedException in Failure, never anything else.
        for (var round = 0; round < 200; ++round)
        {
            var world = new World(WorldConfig.Default(48, 48) with { Mode = RuleMode.Ecosystem });
            world.Randomize(new SeedConfig { Species = 1, DensityPermille = 300, Seed = (ulong)(round + 1) });
            using var clock = new SimClock(world, 2000) { CaptureHeat = round % 2 == 0 };
            clock.Start();

            // Dispose underneath a running clock: the correct outcome is either
            // a clean stop or an ObjectDisposedException recorded as Failure,
            // never memory damage.
            world.Dispose();
            clock.Dispose();

            if (clock.Failure is not null)
            {
                Assert.IsAssignableFrom<ObjectDisposedException>(clock.Failure);
            }
        }
    }

    [Fact]
    public void Disposing_waits_for_an_in_flight_native_call_instead_of_freeing_the_world()
    {
        using var world = new World(WorldConfig.Default(32, 32));

        // Holding the gate is exactly the state Dispose must not walk through:
        // it stands in for "another thread is inside ecosim_world_step". The
        // interop signatures take IntPtr rather than SafeHandle, so the runtime
        // adds no ref of its own and nothing else keeps the world alive.
        //
        // Freed-but-not-yet-reused memory usually still behaves, which is why a
        // test that merely interleaves dispose with stepping cannot fail
        // reliably. This one observes the ordering instead, so removing the lock
        // from ReleaseHandle breaks it deterministically.
        world.Native.Enter();
        try
        {
            var dispose = Task.Run(() => world.Dispose());

            Assert.False(dispose.Wait(TimeSpan.FromMilliseconds(500)),
                         "Dispose released the native world while a call was still in flight");

            world.Native.Exit();
            Assert.True(dispose.Wait(TimeSpan.FromSeconds(10)),
                        "Dispose did not finish once the in-flight call returned");
        }
        catch
        {
            // Never leave the gate held: Monitor ownership is per-thread, and an
            // unbalanced Enter would wedge every later test in this class.
            world.Native.Exit();
            throw;
        }
    }

    [Fact]
    public void Hammering_worlds_from_many_threads_survives()
    {
        // Widens every interleaving the app can produce: several worlds, each
        // stepped, captured, rendered from and destroyed by different threads
        // at once. If anything across the managed/native boundary can corrupt
        // memory, this is where it shows up - and as an assertion rather than
        // as the intermittent test-host crash it was first reported as.
        //
        // Threads is pinned to 2 rather than left at the default: the default
        // is hardware_concurrency, and eight workers each spawning and joining
        // a full pool per round measures thread churn instead of the boundary.
        const int workers = 6;
        var seconds = 3;
        var failures = new System.Collections.Concurrent.ConcurrentQueue<string>();
        var rounds = new int[workers];
        var threads = new Thread[workers];
        var stopAt = DateTime.UtcNow.AddSeconds(seconds);

        for (var worker = 0; worker < workers; ++worker)
        {
            var id = worker;
            var seedBase = (ulong)(id + 1) * 1000;
            var t = new Thread(() =>
            {
                try
                {
                    var round = 0;
                    while (DateTime.UtcNow < stopAt)
                    {
                        var size = 24 + (round % 4) * 8;
                        using var world = new World(WorldConfig.Default(size, size) with
                        {
                            Mode = round % 2 == 0 ? RuleMode.Ecosystem : RuleMode.BirthSurvive,
                            Threads = 2,
                        });
                        world.Randomize(new SeedConfig
                        {
                            Species = 1 + (round % 3),
                            DensityPermille = 250,
                            Seed = seedBase + (ulong)round,
                        });

                        var buffer = new FrameBuffer();
                        var palette = round % 3 == 0
                            ? Palette.Default.WithLayer(RenderLayer.Energy)
                            : Palette.Default;

                        // One thread reads while this one steps, so the borrowed
                        // grid pointers and the double buffer are genuinely
                        // contended rather than politely alternating.
                        var pump = Task.Run(() =>
                        {
                            for (var i = 0; i < 30; ++i)
                            {
                                _ = world.CaptureFor(palette);
                                _ = world.EnergyHistogram(12);
                                _ = world.History(64);
                                world.RenderFrame(buffer, palette);
                            }
                        });

                        for (var i = 0; i < 30; ++i)
                        {
                            world.Step(2);
                            _ = world.Stats();
                        }
                        pump.Wait();
                        world.Dispose();
                        rounds[id] = ++round;
                    }
                }
                catch (Exception ex) { failures.Enqueue($"worker {id}: {ex.GetType().Name}: {ex.Message}"); }
            });
            t.IsBackground = true;
            threads[id] = t;
            t.Start();
        }

        var hung = new List<int>();
        for (var i = 0; i < workers; ++i)
        {
            // Generous on purpose: a thread that needs this long is stuck, and
            // "stuck" is worth failing on just as much as "corrupted".
            if (!threads[i].Join(TimeSpan.FromSeconds(45))) hung.Add(i);
        }

        Assert.True(hung.Count == 0,
                    $"workers never returned: [{string.Join(", ", hung)}], rounds [{string.Join(", ", rounds)}]");
        Assert.Empty(failures);
    }

    [Fact]
    public void SimClock_and_manual_steps_share_the_lock_without_deadlock()
    {
        using var world = new World(new WorldConfig { Width = 64, Height = 64 });
        using var clock = new SimClock(world, 200);
        clock.Start();

        var buffer = new FrameBuffer();
        for (var i = 0; i < 50; ++i)
        {
            world.Step(1);
            world.RenderFrame(buffer, Palette.Default);
        }

        clock.StopAsync().Wait(TimeSpan.FromSeconds(10));
        Assert.False(clock.IsRunning);
        Assert.Null(clock.Failure);
    }
}
