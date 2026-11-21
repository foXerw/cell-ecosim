using Ecosim.Core;

namespace Ecosim.Tests;

public class ViewportTests
{
    [Fact]
    public void Screen_to_cell_is_floor_division_with_offset()
    {
        var vp = new Viewport { CellSize = 4, OffsetX = 10, OffsetY = 20 };
        Assert.Equal((2, 3), vp.ScreenToCell(10 + 8, 20 + 12));   // exact corner
        Assert.Equal((2, 3), vp.ScreenToCell(10 + 11.9, 20 + 15.9)); // inside the cell
        Assert.Equal((1, 2), vp.ScreenToCell(10 + 7.99, 20 + 11.99));
        Assert.Equal((-1, -1), vp.ScreenToCell(9.9, 19.9));       // outside, still maps
    }

    [Fact]
    public void Zoom_at_cursor_keeps_the_cell_under_the_cursor_still_under_it()
    {
        var vp = Viewport.Fit(512, 512, 800, 600);
        const double cursorX = 321.5, cursorY = 244.25;
        var before = vp.ScreenToCell(cursorX, cursorY);

        for (var i = 0; i < 6; ++i)
        {
            vp = vp.ZoomAt(cursorX, cursorY, 1.5);
            Assert.Equal(before, vp.ScreenToCell(cursorX, cursorY));
        }
        for (var i = 0; i < 6; ++i)
        {
            vp = vp.ZoomAt(cursorX, cursorY, 1 / 1.5);
            Assert.Equal(before, vp.ScreenToCell(cursorX, cursorY));
        }
    }

    [Fact]
    public void Cell_size_clamps_to_1_and_64()
    {
        var vp = new Viewport { CellSize = 40 };
        for (var i = 0; i < 20; ++i) vp = vp.ZoomAt(0, 0, 2);
        Assert.Equal(Viewport.MaxCellSize, vp.CellSize);

        vp = new Viewport { CellSize = 2 };
        for (var i = 0; i < 20; ++i) vp = vp.ZoomAt(0, 0, 0.5);
        Assert.Equal(Viewport.MinCellSize, vp.CellSize);
    }

    [Fact]
    public void Visible_cells_is_clipped_to_grid_bounds_at_each_corner()
    {
        var vp = new Viewport { CellSize = 10, OffsetX = -35, OffsetY = -45 };
        // Screen (0,0) is cell (3,4); a 100x100 canvas reaches cell (13,14).
        Assert.Equal((3, 4, 14, 15), vp.VisibleCells(100, 100, 100, 100));

        // Same viewport on a tiny grid: clipped on every side.
        Assert.Equal((3, 4, 8, 8), vp.VisibleCells(8, 8, 100, 100));

        // Fully off-grid to the left: empty intersection.
        var far = new Viewport { CellSize = 10, OffsetX = 5000, OffsetY = 0 };
        Assert.Equal((0, 0, 0, 10), far.VisibleCells(10, 10, 100, 100));
    }

    [Fact]
    public void Fit_centres_and_picks_the_largest_cell_size_that_fits()
    {
        var vp = Viewport.Fit(100, 100, 800, 600);
        Assert.Equal(6, vp.CellSize);              // 600/100, not 800/100
        Assert.Equal(100, vp.OffsetX);             // (800 - 600)/2
        Assert.Equal(0, vp.OffsetY);
        Assert.Equal((0, 0), vp.ScreenToCell(100, 0));
        Assert.Equal((99, 99), vp.ScreenToCell(699.9, 599.9));  // last cell of the grid
        Assert.Equal((116, 99), vp.ScreenToCell(799.9, 599.9)); // right margin: off-grid
    }

    [Fact]
    public void Fit_degenerates_safely_on_nonsense_sizes()
    {
        var vp = Viewport.Fit(0, 10, 100, 100);
        Assert.Equal(Viewport.MinCellSize, vp.CellSize);
    }

    [Fact]
    public void Destination_rect_and_screen_to_cell_are_exact_inverses()
    {
        // The view lays the bitmap out from DestinationRect; a click is mapped
        // with ScreenToCell. If those two ever disagree, the brush lands away
        // from the cursor - which is exactly the bug this test prevents.
        foreach (var (grid, canvas, factor) in new[]
                 {
                     (512, (800.0, 600.0), 1.0),
                     (512, (800.0, 600.0), 1.5),
                     (512, (1100.0, 900.0), 0.25),
                     (64, (900.0, 700.0), 3.0),
                 })
        {
            var vp = Viewport.Fit(grid, grid, canvas.Item1, canvas.Item2);
            for (var i = 0; i < 40; ++i) vp = vp.ZoomAt(canvas.Item1 * 0.4, canvas.Item2 * 0.6, factor);

            var dest = vp.DestinationRect(grid, grid);
            Assert.Equal(vp.OffsetX, dest.X);
            Assert.Equal(vp.OffsetY, dest.Y);
            Assert.Equal(grid * vp.CellSize, dest.Width, 6);

            // The centre of each corner cell must map back to that cell.
            foreach (var (cx, cy) in new[] { (0, 0), (grid / 2, grid / 2), (grid - 1, grid - 1) })
            {
                double screenX = dest.X + (cx + 0.5) * vp.CellSize;
                double screenY = dest.Y + (cy + 0.5) * vp.CellSize;
                Assert.Equal((cx, cy), vp.ScreenToCell(screenX, screenY));
            }
        }
    }

    [Fact]
    public void Destination_rect_scales_the_whole_grid_into_the_canvas()
    {
        var vp = Viewport.Fit(512, 512, 1024, 512);
        var dest = vp.DestinationRect(512, 512);
        Assert.Equal(512, dest.Width, 6);
        Assert.Equal(512, dest.Height, 6);
        Assert.Equal(256, dest.X, 6);   // centred horizontally in the canvas
        Assert.Equal(0, dest.Y, 6);
    }

    [Fact]
    public void Panning_moves_the_offset_and_nothing_else()
    {
        var vp = new Viewport { CellSize = 3, OffsetX = 5, OffsetY = 7 };
        var moved = vp.Panned(-2, 4);
        Assert.Equal(3, vp.CellSize);
        Assert.Equal(3, moved.OffsetX);
        Assert.Equal(11, moved.OffsetY);
    }
}
