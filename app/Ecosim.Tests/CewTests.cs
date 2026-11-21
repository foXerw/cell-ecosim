using Ecosim.Core;

namespace Ecosim.Tests;

public class CewTests
{
    private static World Ecosystem(int size = 24, ulong seed = 31)
    {
        var world = new World(WorldConfig.Default(size, size) with { Mode = RuleMode.Ecosystem });
        world.Randomize(new SeedConfig { Species = 1, DensityPermille = 300, Seed = seed });
        return world;
    }

    [Fact]
    public void A_saved_world_reads_back_identical()
    {
        using var source = Ecosystem();
        var light = World.ParamDescriptions.First(d => d.Name == "light");
        source.SetParam(light.Index, 99);
        source.Step(15);

        var file = source.SaveCew();
        Assert.True(file.Length > 40);
        Assert.Equal("CEWS", System.Text.Encoding.ASCII.GetString(file, 0, 4));

        using var target = Ecosystem(size: 24, seed: 77);
        target.Step(3);
        target.LoadCew(file);

        Assert.Equal(source.Hash(), target.Hash());
        Assert.Equal(source.Generation, target.Generation);
        Assert.Equal(99, target.GetParam(light.Index));
    }

    [Fact]
    public void A_food_web_keeps_its_species_across_a_save()
    {
        // RLE could not do this: it is single-state. The whole point of .cew is
        // that species, energy and resource come back.
        using var source = Ecosystem(size: 32, seed: 21);
        source.Step(60);
        var before = source.Stats();
        Assert.True(before.CountOf(2) > 0);

        using var target = Ecosystem(size: 32, seed: 5);
        target.LoadCew(source.SaveCew());
        var after = target.Stats();

        Assert.Equal(before.Alive, after.Alive);
        Assert.Equal(before.CountOf(1), after.CountOf(1));
        Assert.Equal(before.CountOf(2), after.CountOf(2));
        Assert.Equal(before.CountOf(3), after.CountOf(3));
    }

    [Fact]
    public void A_truncated_file_says_it_is_corrupt()
    {
        using var source = Ecosystem();
        source.Step(6);
        var file = source.SaveCew();
        var half = file[..(file.Length / 2)];

        using var target = Ecosystem();
        var ex = Assert.Throws<EcosimException>(() => target.LoadCew(half));
        Assert.Contains("损坏", ex.Message);
    }

    [Fact]
    public void A_board_size_mismatch_names_both_sizes()
    {
        using var source = Ecosystem(size: 8);
        var file = source.SaveCew();

        using var target = Ecosystem(size: 16);
        var ex = Assert.Throws<EcosimException>(() => target.LoadCew(file));
        Assert.Equal(AbiConstants.ErrRange, ex.Code);
        Assert.Contains("64", ex.Message);
        Assert.Contains("256", ex.Message);
    }

    [Fact]
    public void The_two_formats_do_not_read_each_other()
    {
        using var source = Ecosystem();
        source.Step(4);
        var cew = source.SaveCew();

        // .cew into the RLE reader: the managed parser rejects it.
        Assert.Throws<FormatException>(() => RleCodec.Decode(
            System.Text.Encoding.UTF8.GetString(cew)));

        // RLE into the .cew reader: no magic, so it says so.
        using var target = Ecosystem();
        var ex = Assert.Throws<EcosimException>(
            () => target.LoadCew(System.Text.Encoding.UTF8.GetBytes("x = 1, y = 1\nb!\n")));
        Assert.Contains("CEWS", ex.Message);
    }

    [Fact]
    public void An_empty_file_is_refused_rather_than_treated_as_a_blank_board()
    {
        using var world = Ecosystem();
        Assert.Throws<ArgumentNullException>(() => world.LoadCew(null!));

        var ex = Assert.Throws<EcosimException>(() => world.LoadCew(Array.Empty<byte>()));
        Assert.Contains("load_cew", ex.Message);
    }
}
