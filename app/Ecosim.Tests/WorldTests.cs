using Ecosim.Core;

namespace Ecosim.Tests;

public class WorldTests
{
    [Fact]
    public void Step_advances_generation_by_the_requested_count()
    {
        using var world = new World(WorldConfig.Default(16, 16));
        Assert.Equal(0, world.Generation);
        world.Step(3);
        Assert.Equal(3, world.Generation);
        world.Step(0);
        Assert.Equal(3, world.Generation);
        world.Step(2);
        Assert.Equal(5, world.Generation);
    }

    [Fact]
    public void Negative_step_throws_with_the_native_diagnostic()
    {
        using var world = new World(WorldConfig.Default(8, 8));
        var ex = Assert.Throws<EcosimException>(() => world.Step(-1));
        Assert.Contains("n_steps", ex.Message);
        Assert.Equal(-1, ex.Code);
    }

    [Fact]
    public void Bad_dimensions_are_rejected_at_construction_with_a_readable_message()
    {
        var ex = Assert.Throws<EcosimException>(() => new World(WorldConfig.Default(0, 8)));
        Assert.Contains("网格尺寸", ex.Message);

        var tooBig = Assert.Throws<EcosimException>(
            () => new World(WorldConfig.Default(AbiConstants.MaxDim + 1, 8)));
        Assert.Contains("超出上限", tooBig.Message);
    }

    [Fact]
    public void Ecosystem_mode_is_not_available_yet_and_says_so()
    {
        var cfg = WorldConfig.Default(8, 8);
        cfg = cfg with { Mode = RuleMode.Ecosystem };
        var ex = Assert.Throws<EcosimException>(() => new World(cfg));
        Assert.Contains("M2", ex.Message);
    }

    [Fact]
    public void Dispose_is_idempotent_and_double_dispose_is_safe()
    {
        var world = new World(WorldConfig.Default(8, 8));
        world.Dispose();
        world.Dispose();
        Assert.Throws<ObjectDisposedException>(() => world.Step(1));
    }
}
