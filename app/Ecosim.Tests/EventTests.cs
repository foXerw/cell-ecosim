using Ecosim.Core;

namespace Ecosim.Tests;

public class EventTests
{
    private static World StarvedGrazers()
    {
        var world = new World(WorldConfig.Default(16, 16) with { Mode = RuleMode.Ecosystem });
        world.Randomize(new SeedConfig { Species = 2, DensityPermille = 300, Seed = 2 });
        return world;
    }

    [Fact]
    public void Nothing_is_reported_before_the_first_step()
    {
        using var world = StarvedGrazers();
        Assert.Empty(world.PollEvents());
    }

    [Fact]
    public void A_starving_species_produces_exactly_one_extinction()
    {
        using var world = StarvedGrazers();
        var kinds = new List<SimEventKind>();
        for (var i = 0; i < 20; ++i)
        {
            world.Step(20);
            kinds.AddRange(world.PollEvents().Select(e => e.Kind));
        }

        Assert.Contains(SimEventKind.Extinction, kinds);
        Assert.Single(kinds.Where(k => k == SimEventKind.Extinction));
    }

    [Fact]
    public void Polling_drains_the_queue()
    {
        using var world = StarvedGrazers();
        for (var i = 0; i < 12; ++i) world.Step(20);

        var first = world.PollEvents();
        var second = world.PollEvents();
        Assert.NotEmpty(first);
        Assert.Empty(second);
    }

    [Fact]
    public void Events_carry_the_generation_they_happened_on()
    {
        using var world = StarvedGrazers();
        SimEvent? extinction = null;
        for (var i = 0; i < 20 && extinction is null; ++i)
        {
            world.Step(10);
            extinction = world.PollEvents().FirstOrDefault(e => e.Kind == SimEventKind.Extinction);
        }

        Assert.NotNull(extinction);
        Assert.Equal(2, extinction!.Species);
        Assert.Equal(0, extinction.Count);
        Assert.InRange(extinction.Generation, 1, 200);
        Assert.Contains("物种 2", extinction.Describe(null));
        Assert.Contains("灭绝", extinction.Describe("虫"));
    }

    [Fact]
    public void Life_mode_reports_extinction_too()
    {
        // A single blinker at the edge of a bounded 8x8 board dies out; the
        // engine should notice even though no ecosystem is running.
        using var world = new World(WorldConfig.Default(8, 8) with
        {
            Topology = GridTopology.Bounded,
        });
        world.Clear();
        world.Paint(new[] { new PaintOp(0, 0, 3, 1, 1) });

        SimEvent? extinction = null;
        for (var i = 0; i < 10 && extinction is null; ++i)
        {
            world.Step(5);
            extinction = world.PollEvents().FirstOrDefault(e => e.Kind == SimEventKind.Extinction);
        }

        Assert.NotNull(extinction);
        Assert.Equal(1, extinction!.Species);
    }
}
