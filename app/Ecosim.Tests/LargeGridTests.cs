using System.Diagnostics;
using Ecosim.Core;

namespace Ecosim.Tests;

/// <summary>
/// Review Focus #3: the app must stay usable on the largest supported grid, so
/// the budgets the design promises are pinned here rather than in a manual
/// checklist.
/// </summary>
public class LargeGridTests
{
    [Fact]
    public void Step_and_render_2048_stay_within_the_frame_budget()
    {
        using var world = new World(new WorldConfig { Width = 2048, Height = 2048, Threads = 0 });
        world.Randomize(new SeedConfig { Species = 1, DensityPermille = 200, Seed = 1234 });

        var stopwatch = Stopwatch.StartNew();
        world.Step(5);
        var stepMs = stopwatch.Elapsed.TotalMilliseconds / 5;

        var buffer = new FrameBuffer();
        stopwatch.Restart();
        world.RenderFrame(buffer, Palette.Default);
        var renderMs = stopwatch.Elapsed.TotalMilliseconds;

        Assert.True(stepMs < 100, $"ms_per_gen {stepMs:F1} 超过 100ms 预算");
        Assert.True(renderMs < 120, $"render {renderMs:F1}ms 超过预算");
        Assert.Equal(2048 * 2048 * 4, buffer.Bytes.Length);
    }

    [Fact]
    public void Custom_masks_are_honoured_end_to_end()
    {
        var seed = new SeedConfig { Species = 1, DensityPermille = 250, Seed = 88 };
        using var life = new World(WorldConfig.Default(96, 96));
        using var highLife = new World(WorldConfig.HighLife(96, 96));
        life.Randomize(seed);
        highLife.Randomize(seed);
        Assert.Equal(life.Hash(), highLife.Hash());  // same soup in, same cells

        life.Step(40);
        highLife.Step(40);
        Assert.NotEqual(life.Hash(), highLife.Hash());  // B36/S23 diverges (replicators)
    }
}
