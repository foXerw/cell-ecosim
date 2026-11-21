using Ecosim.Core;

namespace Ecosim.Tests;

public class HistoryTests
{
    private static World Life(int size = 16)
    {
        var world = new World(WorldConfig.Default(size, size));
        world.Randomize(new SeedConfig { Species = 1, DensityPermille = 300, Seed = 2 });
        return world;
    }

    [Fact]
    public void A_fresh_world_has_no_history()
    {
        using var world = Life();
        Assert.Equal(0, world.HistoryLength);
        Assert.Empty(world.History(10));
    }

    [Fact]
    public void One_sample_per_generation_oldest_first()
    {
        using var world = Life();
        world.Step(50);

        Assert.Equal(50, world.HistoryLength);
        var samples = world.History(50);
        Assert.Equal(50, samples.Count);
        Assert.Equal(1, samples[0].Generation);
        Assert.Equal(50, samples[^1].Generation);
        Assert.True(samples.Zip(samples.Skip(1)).All(pair => pair.Second.Generation > pair.First.Generation));
    }

    [Fact]
    public void The_newest_sample_matches_a_fresh_stats_read()
    {
        using var world = Life();
        world.Step(37);
        var newest = world.History(1)[0];
        var stats = world.Stats();

        Assert.Equal(stats.Generation, newest.Generation);
        Assert.Equal(stats.Alive, newest.Alive);
        Assert.Equal(stats.SpeciesCounts, newest.SpeciesCounts);
    }

    [Fact]
    public void Asking_for_more_than_exists_returns_everything()
    {
        using var world = Life();
        world.Step(5);
        Assert.Equal(5, world.History(4096).Count);
        Assert.Empty(world.History(0));
    }

    [Fact]
    public void The_ring_drops_the_oldest_when_it_wraps()
    {
        using var world = Life(8);
        const int capacity = 4096;
        world.Step(capacity + 104);

        var samples = world.History(capacity);
        Assert.Equal(capacity, samples.Count);
        // The ten-odd earliest generations are gone; the newest is still here.
        Assert.Equal(capacity + 104, samples[^1].Generation);
        Assert.Equal(105, samples[0].Generation);
    }

    [Fact]
    public void History_records_the_ecosystem_pipeline_too()
    {
        using var world = new World(WorldConfig.Default(32, 32) with { Mode = RuleMode.Ecosystem });
        world.Randomize(new SeedConfig { Species = 1, DensityPermille = 300, Seed = 4 });
        world.Step(20);

        var samples = world.History(20);
        Assert.Equal(20, samples.Count);
        Assert.Equal(world.Stats().Alive, samples[^1].Alive);
        Assert.True(samples.Sum(s => s.Alive) > 0);
    }

    [Fact]
    public void History_does_not_depend_on_thread_count()
    {
        var config = WorldConfig.Default(64, 64) with { Threads = 1 };
        using var serial = new World(config);
        using var parallelWorld = new World(config with { Threads = 4 });
        var seed = new SeedConfig { Species = 1, DensityPermille = 300, Seed = 6 };
        serial.Randomize(seed);
        parallelWorld.Randomize(seed);

        serial.Step(60);
        parallelWorld.Step(60);

        Assert.Equal(serial.History(60).Select(s => s.Alive),
                     parallelWorld.History(60).Select(s => s.Alive));
    }
}
