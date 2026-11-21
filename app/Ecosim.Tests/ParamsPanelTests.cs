using Ecosim.Core;

namespace Ecosim.Tests;

public class ParamsPanelTests
{
    private static World Ecosystem() =>
        new(WorldConfig.Default(16, 16) with { Mode = RuleMode.Ecosystem });

    [Fact]
    public void Slider_count_equals_the_description_table()
    {
        using var world = Ecosystem();
        var model = new ParamsModel(world);
        Assert.Equal(World.ParamDescriptions.Count, model.Sliders.Count);
        Assert.Equal(model.Sliders.Count, model.Groups.Sum(g => g.Sliders.Count));
    }

    [Fact]
    public void Groups_split_global_from_per_species_sections()
    {
        using var world = Ecosystem();
        var model = new ParamsModel(world);
        Assert.Contains(model.Groups, g => g.Section == "全局");
        Assert.Contains(model.Groups, g => g.Section.StartsWith("物种 "));
        Assert.DoesNotContain(model.Groups, g => g.Sliders.Count == 0);
    }

    [Fact]
    public void Moving_a_slider_writes_through_to_the_engine()
    {
        using var world = Ecosystem();
        var model = new ParamsModel(world);
        var light = model.Find("light")!;

        light.Value = 120;
        Assert.Equal(120, world.GetParam(light.Description.Index));
        Assert.Null(light.Error);
        Assert.Equal(120, light.Value);
    }

    [Fact]
    public void Values_outside_the_range_are_clamped_not_rejected()
    {
        using var world = Ecosystem();
        var model = new ParamsModel(world);
        var light = model.Find("light")!;

        light.Value = light.Maximum + 5_000;
        Assert.Equal(light.Maximum, light.Value);
        Assert.Equal((int)light.Maximum, world.GetParam(light.Description.Index));
        Assert.Null(light.Error);

        light.Value = light.Minimum - 5_000;
        Assert.Equal(light.Minimum, light.Value);
    }

    [Fact]
    public void A_new_world_reads_back_the_engine_defaults()
    {
        using (var first = Ecosystem())
        {
            var model = new ParamsModel(first);
            model.Find("light")!.Value = 7;
        }
        using var second = Ecosystem();
        var fresh = new ParamsModel(second);
        Assert.Equal(World.ParamDescriptions.First(d => d.Name == "light").Default,
                     fresh.Find("light")!.Value);
    }

    [Fact]
    public void Refresh_picks_up_a_value_written_behind_the_panel_s_back()
    {
        using var world = Ecosystem();
        var model = new ParamsModel(world);
        var fertility = model.Find("fertility")!;

        world.SetParam(fertility.Description.Index, 42);
        Assert.NotEqual(42, fertility.Value);

        model.Refresh();
        Assert.Equal(42, fertility.Value);
    }

    [Fact]
    public void Slider_step_and_labels_come_from_the_engine()
    {
        using var world = Ecosystem();
        var model = new ParamsModel(world);
        var light = model.Find("light")!;
        Assert.True(light.Step > 0);
        Assert.False(string.IsNullOrWhiteSpace(light.Title));
        Assert.Equal(light.Description.Label, light.Title);

        var metabolism = model.Find("metabolism_grazer")!;
        Assert.Equal("物种 2", metabolism.Section);
        Assert.False(metabolism.Description.IsPermille);
    }
}
