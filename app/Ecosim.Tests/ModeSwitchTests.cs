using Ecosim.Core;

namespace Ecosim.Tests;

/// <summary>
/// Switching rule modes rebuilds the world, so the living cells have to travel
/// across. RLE cannot carry them (it is single-state by design), which is what
/// the bulk species-plane loader is for.
/// </summary>
public class ModeSwitchTests
{
    [Fact]
    public void Cells_carry_across_life_to_ecosystem_and_back()
    {
        using var life = new World(WorldConfig.Default(48, 48));
        life.Randomize(new SeedConfig { Species = 1, DensityPermille = 300, Seed = 5 });
        var carry = life.Capture();

        using var ecosystem = new World(WorldConfig.Default(48, 48) with
        {
            Mode = RuleMode.Ecosystem,
        });
        ecosystem.LoadSpecies(carry.Species);
        Assert.Equal(carry.Species, ecosystem.Capture().Species);
        Assert.Equal(carry.Alive, ecosystem.Stats().Alive);

        var back = ecosystem.Capture();
        using var life2 = new World(WorldConfig.Default(48, 48));
        life2.LoadSpecies(back.Species);
        Assert.Equal(back.Species, life2.Capture().Species);
    }

    [Fact]
    public void Carried_cells_arrive_with_energy_not_zero()
    {
        // If load_species left energy at zero, every cell would starve on the
        // first generation of the ecosystem pipeline.
        using var source = new World(WorldConfig.Default(32, 32));
        source.Randomize(new SeedConfig { Species = 1, DensityPermille = 400, Seed = 11 });
        var carry = source.Capture();

        using var target = new World(WorldConfig.Default(32, 32) with { Mode = RuleMode.Ecosystem });
        target.LoadSpecies(carry.Species);
        target.Step(1);
        Assert.True(target.Stats().Alive > 0, "带过来的细胞应当带着初始能量");
    }

    [Fact]
    public void A_plane_of_the_wrong_size_is_refused()
    {
        using var world = new World(WorldConfig.Default(8, 8));
        var ex = Assert.Throws<EcosimException>(() => world.LoadSpecies(new byte[64 - 1]));
        Assert.Equal(AbiConstants.ErrRange, ex.Code);
        Assert.Contains("不符", ex.Message);

        Assert.Throws<EcosimException>(() => world.LoadSpecies(Array.Empty<byte>()));
    }

    [Fact]
    public void Ecosystem_mode_reports_per_species_counts()
    {
        using var world = new World(WorldConfig.Default(24, 24) with { Mode = RuleMode.Ecosystem });
        world.Clear();
        world.Paint(new[]
        {
            new PaintOp(0, 0, 2, 2, 1),
            new PaintOp(4, 4, 5, 6, 2),
            new PaintOp(8, 8, 10, 9, 3),
        });
        var stats = world.Stats();
        Assert.Equal(4, stats.CountOf(1));
        Assert.Equal(2, stats.CountOf(2));
        Assert.Equal(2, stats.CountOf(3));
        Assert.Equal(8, stats.Alive);
    }
}
