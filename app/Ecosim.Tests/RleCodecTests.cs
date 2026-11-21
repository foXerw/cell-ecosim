using Ecosim.Core;

namespace Ecosim.Tests;

public class RleCodecTests
{
    private const string Glider = "x = 3, y = 3, rule = B3/S23\nbob$2bo$3o!\n";
    private const string RPentomino = "x = 3, y = 3, rule = B3/S23\nb2o$2o$bo!\n";

    [Fact]
    public void Glider_rle_decodes_to_expected_cells()
    {
        var doc = RleCodec.Decode(Glider);
        Assert.Equal(3, doc.Width);
        Assert.Equal(3, doc.Height);
        Assert.Equal("B3/S23", doc.Rule);
        var expected = new[] { (0, 2), (1, 0), (1, 2), (2, 1), (2, 2) };  // sorted by (x, y)
        Assert.Equal(expected, doc.Cells.OrderBy(c => c).ToArray());
    }

    [Fact]
    public void Comments_and_wrapped_rows_are_ignored()
    {
        var doc = RleCodec.Decode("#N glider\n#C by hand\n" + Glider);
        Assert.Equal(5, doc.Cells.Count);
    }

    [Fact]
    public void Encode_then_decode_is_the_identity_on_cells()
    {
        var doc = RleCodec.Decode(Glider);
        var encoded = RleCodec.Encode(0, 0, doc.Cells);
        var again = RleCodec.Decode(encoded);
        Assert.Equal(doc.Cells.OrderBy(c => c).ToArray(), again.Cells.OrderBy(c => c).ToArray());
        Assert.Equal(doc.Width, again.Width);
        Assert.Equal(doc.Height, again.Height);
    }

    [Theory]
    [InlineData("10a!")]
    [InlineData("3o$3o$3o")]
    [InlineData("")]
    [InlineData("   \n  ")]
    public void Garbage_is_rejected_with_a_readable_message(string text)
    {
        Assert.Throws<FormatException>(() => RleCodec.Decode(text));
    }

    [Fact]
    public void An_absurd_declared_size_is_refused_instead_of_allocating()
    {
        // A parser that trusts its input's size claims will happily try to
        // materialise 16 million cells. Feeding a binary archive to this
        // reader did exactly that and took the test host down with it.
        // one run too long
        Assert.Throws<FormatException>(() => RleCodec.Decode("16000000b!"));
        // many runs, but a declared live total beyond any board we support
        Assert.Throws<FormatException>(() => RleCodec.Decode("5000000o!"));
        // and a width claim beyond the same bound
        Assert.Throws<FormatException>(() => RleCodec.Decode("1o$" + new string('9', 7) + "b!"));

        // A large but plausible file still parses, and dead space costs O(1)
        // rather than one iteration per cell.
        var doc = RleCodec.Decode("1000000b!");
        Assert.Empty(doc.Cells);
        Assert.Equal(1_000_000, doc.Width);
        Assert.Equal(100_000, RleCodec.Decode("100000o!").Cells.Count);
    }

    [Fact]
    public void Managed_and_native_decoders_agree_cell_for_cell()
    {
        var doc = RleCodec.Decode(Glider);
        using var viaNative = new World(WorldConfig.Default(16, 16));
        viaNative.FromRle(Glider);

        using var viaManaged = new World(WorldConfig.Default(16, 16));
        viaManaged.Paint(doc.Cells
            .Select(c => new PaintOp(c.X, c.Y, c.X + 1, c.Y + 1, 1))
            .ToList());

        Assert.Equal(viaNative.Hash(), viaManaged.Hash());
    }

    [Fact]
    public void Managed_encoder_output_decodes_identically_in_native()
    {
        var doc = RleCodec.Decode(RPentomino);
        var encoded = RleCodec.Encode(0, 0, doc.Cells);

        using var original = new World(WorldConfig.Default(24, 24));
        original.FromRle(RPentomino);
        using var reEncoded = new World(WorldConfig.Default(24, 24));
        reEncoded.FromRle(encoded);

        Assert.Equal(original.Hash(), reEncoded.Hash());
        original.Step(3);
        reEncoded.Step(3);
        Assert.Equal(original.Hash(), reEncoded.Hash());
    }

    [Fact]
    public void From_rle_at_places_the_pattern_at_the_offset()
    {
        using var atZero = new World(WorldConfig.Default(16, 16));
        atZero.FromRle(Glider);
        using var atOffset = new World(WorldConfig.Default(16, 16));
        atOffset.FromRleAt(Glider, 5, 5);
        Assert.NotEqual(atZero.Hash(), atOffset.Hash());

        // Same world, same cells, different corner: the shapes must still match.
        var zero = atZero.ToRle();
        var offset = atOffset.ToRle();
        Assert.Equal(zero, offset);
    }
}

public class PatternLibraryTests
{
    [Fact]
    public void Library_ships_the_verified_patterns()
    {
        var ids = PatternLibrary.All.Select(p => p.Id).ToArray();
        Assert.Contains("block", ids);
        Assert.Contains("blinker", ids);
        Assert.Contains("glider", ids);
        Assert.Contains("r_pentomino", ids);
        Assert.Contains("beehive", ids);
    }

    [Fact]
    public void Every_pattern_decodes_to_a_nonempty_box()
    {
        foreach (var pattern in PatternLibrary.All)
        {
            var doc = RleCodec.Decode(pattern.Rle);
            Assert.True(doc.Cells.Count > 0, $"{pattern.Id} 解码后没有活细胞");
            Assert.True(doc.Width > 0 && doc.Height > 0);
        }
    }

    [Fact]
    public void Find_is_case_insensitive_and_unknown_returns_null()
    {
        Assert.NotNull(PatternLibrary.Find("GLIDER"));
        Assert.Null(PatternLibrary.Find("gosper_gun"));
    }
}
