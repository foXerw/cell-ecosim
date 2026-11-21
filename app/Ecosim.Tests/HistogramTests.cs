using Ecosim.Core;

namespace Ecosim.Tests;

public class HistogramTests
{
    private static World Ecosystem(int size = 24, ulong seed = 3)
    {
        var world = new World(WorldConfig.Default(size, size) with { Mode = RuleMode.Ecosystem });
        world.Randomize(new SeedConfig { Species = 1, DensityPermille = 300, Seed = seed });
        return world;
    }

    [Fact]
    public void Buckets_sum_to_the_living_population()
    {
        using var world = Ecosystem();
        world.Step(6);
        var snapshot = world.Capture(includeHeat: true);
        var histogram = world.EnergyHistogram(12, snapshot);

        Assert.Equal(12, histogram.Length);
        Assert.Equal(snapshot.Alive, histogram.Sum());
        Assert.Equal(world.Stats().Alive, histogram.Sum());
    }

    [Fact]
    public void An_empty_board_has_no_energy_anywhere()
    {
        using var world = Ecosystem();
        world.Clear();
        Assert.Equal(0, world.EnergyHistogram(8).Sum());
    }

    [Fact]
    public void Bucket_count_is_clamped_and_a_thin_snapshot_yields_nothing()
    {
        using var world = Ecosystem();
        Assert.Equal(2, world.EnergyHistogram(1).Length);
        Assert.Equal(64, world.EnergyHistogram(1000).Length);

        // A species-only snapshot has no energy planes to read.
        var thin = world.Capture();
        Assert.Equal(0, world.EnergyHistogram(10, thin).Sum());
    }

    [Fact]
    public void A_mixed_web_keeps_its_energy_in_the_middle_of_the_range()
    {
        // Per-species normalisation is what stops a grazer at 150/400 from
        // looking "healthier" than an alga at 150/240. The energy values
        // themselves are pinned by the native pipeline tests; what this checks
        // is that the mapping stays in range and never dumps everything into
        // the top or bottom bucket.
        using var world = Ecosystem(size: 32, seed: 21);
        world.Step(40);
        var histogram = world.EnergyHistogram(12);

        Assert.Equal(world.Stats().Alive, histogram.Sum());
        Assert.True(histogram[0] + histogram[^1] < histogram.Sum(),
                    "全部细胞都在极端桶里，说明归一化错了");
    }
}
