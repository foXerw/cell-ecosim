using Ecosim.Core;

namespace Ecosim.Tests;

public class StatsTests
{
    [Fact]
    public void Stats_track_paint_step_and_clear()
    {
        using var world = new World(WorldConfig.Default(16, 16));
        world.Paint(new[]
        {
            new PaintOp(0, 0, 3, 1, 1),
            new PaintOp(0, 2, 2, 3, 2),
        });

        var stats = world.Stats();
        Assert.Equal(5, stats.Alive);
        Assert.Equal(3, stats.CountOf(1));
        Assert.Equal(2, stats.CountOf(2));
        Assert.Equal(0, stats.CountOf(3));
        Assert.Equal(0, stats.Generation);

        world.Step(4);
        Assert.Equal(4, world.Stats().Generation);

        world.Clear();
        var cleared = world.Stats();
        Assert.Equal(0, cleared.Alive);
        Assert.Equal(4, cleared.Generation);  // clearing is not rewinding
    }

    [Fact]
    public void Randomize_is_reproducible_per_seed_and_density_is_honoured()
    {
        using var a = new World(WorldConfig.Default(128, 128));
        using var b = new World(WorldConfig.Default(128, 128));
        var seed = new SeedConfig { Species = 1, DensityPermille = 300, Seed = 4242 };
        a.Randomize(seed);
        b.Randomize(seed);
        Assert.Equal(a.Hash(), b.Hash());

        var ratio = a.Stats().Alive / (128.0 * 128.0);
        Assert.InRange(ratio, 0.25, 0.35);

        a.Randomize(seed with { Seed = 7 });
        Assert.NotEqual(a.Hash(), b.Hash());
    }

    [Fact]
    public void Out_of_range_paint_throws_with_the_engine_message()
    {
        using var world = new World(WorldConfig.Default(8, 8));
        var ex = Assert.Throws<EcosimException>(
            () => world.Paint(new[] { new PaintOp(6, 6, 12, 7, 1) }));
        Assert.Equal(AbiConstants.ErrRange, ex.Code);
        Assert.Contains("超出网格", ex.Message);
    }

    [Fact]
    public void Bad_randomize_arguments_are_rejected()
    {
        using var world = new World(WorldConfig.Default(8, 8));
        Assert.Throws<EcosimException>(
            () => world.Randomize(new SeedConfig { Species = 1, DensityPermille = 1500 }));
        Assert.Throws<EcosimException>(
            () => world.Randomize(new SeedConfig { Species = 99, DensityPermille = 100 }));
    }
}
