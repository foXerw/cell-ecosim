namespace Ecosim.Core;

/// <summary>
/// Turns a pointer drag into paint operations. One op per touched cell run, so
/// a long stroke costs one native call instead of one per pixel.
/// </summary>
public static class BrushStroke
{
    /// <summary>Bresenham between two cells, fattened to a disc of
    /// <paramref name="radius"/> cells; identical cells are emitted once.</summary>
    public static IReadOnlyList<PaintOp> Line((int X, int Y) from, (int X, int Y) to,
                                              int species, int radius)
    {
        if (radius < 1) throw new ArgumentOutOfRangeException(nameof(radius), "笔刷半径至少为 1");

        var seen = new HashSet<(int, int)>();
        var ops = new List<PaintOp>();

        void AddCell(int cx, int cy)
        {
            for (var dy = -radius + 1; dy <= radius - 1; ++dy)
            {
                // Disc, not square: |dx| + |dy| <= r-1 keeps the brush round.
                var reach = radius - 1 - Math.Abs(dy);
                for (var dx = -reach; dx <= reach; ++dx)
                {
                    if (!seen.Add((cx + dx, cy + dy))) continue;
                    ops.Add(new PaintOp(cx + dx, cy + dy, cx + dx + 1, cy + dy + 1, species));
                }
            }
        }

        var (x0, y0) = from;
        var (x1, y1) = to;
        var dx = Math.Abs(x1 - x0);
        var dy = -Math.Abs(y1 - y0);
        var sx = x0 < x1 ? 1 : -1;
        var sy = y0 < y1 ? 1 : -1;
        var err = dx + dy;

        while (true)
        {
            AddCell(x0, y0);
            if (x0 == x1 && y0 == y1) break;
            var e2 = 2 * err;
            if (e2 >= dy)
            {
                err += dy;
                x0 += sx;
            }
            if (e2 <= dx)
            {
                err += dx;
                y0 += sy;
            }
        }

        return ops;
    }
}
