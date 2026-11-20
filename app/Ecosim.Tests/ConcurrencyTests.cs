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
