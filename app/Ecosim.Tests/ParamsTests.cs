using Ecosim.Core;

namespace Ecosim.Tests;

public class ParamsTests
{
    private static World Ecosystem() =>
        new(WorldConfig.Default(32, 32) with { Mode = RuleMode.Ecosystem });

    [Fact]
    public void Descriptions_are_complete_and_self_consistent()
    {
        var descs = World.ParamDescriptions;
        Assert.True(descs.Count >= 2, "至少要有光照与地力两个全局参数");
        Assert.Equal(Enumerable.Range(0, descs.Count), descs.Select(d => d.Index));
        foreach (var d in descs)
        {
            Assert.True(d.Min <= d.Default && d.Default <= d.Max, $"{d.Name} 的范围不合理");
            Assert.False(string.IsNullOrWhiteSpace(d.Name));
            Assert.False(string.IsNullOrWhiteSpace(d.Label));
            if (!d.IsPerSpecies) Assert.Equal(0, d.Species);
        }
    }

    [Fact]
    public void Every_parameter_reads_back_its_default_on_a_fresh_world()
    {
        using var world = Ecosystem();
        foreach (var d in World.ParamDescriptions)
        {
            Assert.Equal(d.Default, world.GetParam(d.Index));
        }
    }

    [Fact]
    public void Setting_a_parameter_sticks_and_is_hot()
    {
        using var world = Ecosystem();
        var light = World.ParamDescriptions.First(d => d.Name == "light");

        world.SetParam(light.Index, light.Min);
        Assert.Equal(light.Min, world.GetParam(light.Index));
        world.Step(5);
        Assert.Equal(light.Min, world.GetParam(light.Index));

        world.SetParam(light.Index, light.Max);
        Assert.Equal(light.Max, world.GetParam(light.Index));
    }

    [Fact]
    public void Out_of_range_and_unknown_indexes_are_refused_with_a_useful_message()
    {
        using var world = Ecosystem();
        var light = World.ParamDescriptions.First(d => d.Name == "light");

        var tooBig = Assert.Throws<EcosimException>(
            () => world.SetParam(light.Index, light.Max + 1));
        Assert.Contains("light", tooBig.Message);
        Assert.Contains("允许", tooBig.Message);

        var unknown = Assert.Throws<EcosimException>(() => world.GetParam(9999));
        Assert.Equal(AbiConstants.ErrRange, unknown.Code);
    }

    [Fact]
    public void Parameters_change_the_ecosystem_outcome()
    {
        using var lit = Ecosystem();
        using var dark = Ecosystem();
        var seed = new SeedConfig { Species = 2, DensityPermille = 200, Seed = 2024 };
        lit.Randomize(seed);
        dark.Randomize(seed);

        var light = World.ParamDescriptions.First(d => d.Name == "light");
        var fertility = World.ParamDescriptions.First(d => d.Name == "fertility");
        dark.SetParam(light.Index, 0);
        dark.SetParam(fertility.Index, 0);

        lit.Step(1500);
        dark.Step(1500);

        Assert.True(lit.Stats().Alive > 0, "有光照的世界应当重新长出生产者");
        Assert.Equal(0, dark.Stats().Alive);
        Assert.NotEqual(lit.Hash(), dark.Hash());
    }
}
