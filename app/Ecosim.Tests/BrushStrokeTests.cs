using Ecosim.Core;

namespace Ecosim.Tests;

public class BrushStrokeTests
{
    private static HashSet<(int, int)> CellsOf(IReadOnlyList<PaintOp> ops)
        => ops.Select(o => (o.X0, o.Y0)).ToHashSet();

    [Fact]
    public void Line_covers_each_cell_once_and_is_symmetric()
    {
        var forward = BrushStroke.Line((0, 0), (10, 0), species: 1, radius: 1);
        Assert.Equal(11, forward.Count);
        Assert.Equal(11, CellsOf(forward).Count);  // no duplicate cells

        var backward = BrushStroke.Line((10, 0), (0, 0), species: 1, radius: 1);
        Assert.Equal(CellsOf(forward), CellsOf(backward));
    }

    [Fact]
    public void Diagonal_line_hits_every_cell_on_the_path()
    {
        var ops = BrushStroke.Line((0, 0), (5, 5), species: 1, radius: 1);
        var cells = CellsOf(ops);
        for (var i = 0; i <= 5; ++i) Assert.Contains((i, i), cells);
        Assert.Equal(6, cells.Count);
    }

    [Fact]
    public void Radius_three_paints_a_disc_not_a_square()
    {
        var ops = BrushStroke.Line((10, 10), (10, 10), species: 1, radius: 3);
        var cells = CellsOf(ops);
        // |dx| + |dy| <= radius-1: 1 + 4 + 4 + 4 = 13 cells, corners excluded.
        Assert.Equal(13, cells.Count);
        Assert.Contains((12, 10), cells);
        Assert.DoesNotContain((12, 12), cells);
    }

    [Fact]
    public void Erase_is_just_species_zero()
    {
        var ops = BrushStroke.Line((2, 2), (2, 2), species: 0, radius: 1);
        Assert.Single(ops);
        Assert.Equal(0, ops[0].Species);
    }

    [Fact]
    public void Radius_below_one_is_rejected()
    {
        Assert.Throws<ArgumentOutOfRangeException>(
            () => BrushStroke.Line((0, 0), (1, 1), 1, 0));
    }
}
