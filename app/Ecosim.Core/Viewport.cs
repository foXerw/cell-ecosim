namespace Ecosim.Core;

/// <summary>A rectangle in screen units, kept here so the mapping maths stays
/// free of Avalonia types.</summary>
public readonly record struct ScreenRect(double X, double Y, double Width, double Height);

/// <summary>
/// Pure screen↔grid mapping. Deliberately free of Avalonia types so it can be
/// unit-tested in plain xUnit: the app's bitmap is one pixel per cell, and this
/// struct is the zoom/pan transform on top of it.
/// </summary>
public readonly record struct Viewport
{
    public const double MinCellSize = 1.0;
    public const double MaxCellSize = 64.0;

    /// <summary>Screen pixels per cell (the zoom scale).</summary>
    public double CellSize { get; init; }

    /// <summary>Screen position of the left edge of cell (0, 0).</summary>
    public double OffsetX { get; init; }

    public double OffsetY { get; init; }

    /// <summary>Largest cell size that shows the whole grid, centred.</summary>
    public static Viewport Fit(int gridWidth, int gridHeight, double canvasWidth,
                               double canvasHeight)
    {
        if (gridWidth <= 0 || gridHeight <= 0 || canvasWidth <= 0 || canvasHeight <= 0)
        {
            return new Viewport { CellSize = MinCellSize };
        }
        var size = Math.Clamp(Math.Min(canvasWidth / gridWidth, canvasHeight / gridHeight),
                              MinCellSize, MaxCellSize);
        return new Viewport
        {
            CellSize = size,
            OffsetX = (canvasWidth - gridWidth * size) / 2.0,
            OffsetY = (canvasHeight - gridHeight * size) / 2.0,
        };
    }

    public (int X, int Y) ScreenToCell(double screenX, double screenY) =>
        ((int)Math.Floor((screenX - OffsetX) / CellSize),
         (int)Math.Floor((screenY - OffsetY) / CellSize));

    /// <summary>Half-open cell rectangle currently on screen, clipped to the grid.</summary>
    public (int X0, int Y0, int X1, int Y1) VisibleCells(int gridWidth, int gridHeight,
                                                         double canvasWidth, double canvasHeight)
    {
        var (left, top) = ScreenToCell(0, 0);
        var (right, bottom) = ScreenToCell(canvasWidth, canvasHeight);
        return (Math.Clamp(left, 0, gridWidth),
                Math.Clamp(top, 0, gridHeight),
                Math.Clamp(right + 1, 0, gridWidth),
                Math.Clamp(bottom + 1, 0, gridHeight));
    }

    /// <summary>
    /// Where the grid goes on screen, in the same units ScreenToCell consumes.
    /// The view lays the bitmap out from this rect, so "where a cell is drawn"
    /// and "which cell the cursor is over" cannot drift apart - a test asserts
    /// the two are exact inverses.
    /// </summary>
    public ScreenRect DestinationRect(int gridWidth, int gridHeight)
        => new(OffsetX, OffsetY, gridWidth * CellSize, gridHeight * CellSize);

    /// <summary>Zoom keeping the cell under the cursor under the cursor.</summary>
    public Viewport ZoomAt(double screenX, double screenY, double factor)
    {
        var next = Math.Clamp(CellSize * factor, MinCellSize, MaxCellSize);
        if (Math.Abs(next - CellSize) < 1e-12) return this;

        var cellX = (screenX - OffsetX) / CellSize;
        var cellY = (screenY - OffsetY) / CellSize;
        return this with
        {
            CellSize = next,
            OffsetX = screenX - cellX * next,
            OffsetY = screenY - cellY * next,
        };
    }

    public Viewport Panned(double dx, double dy) =>
        this with { OffsetX = OffsetX + dx, OffsetY = OffsetY + dy };
}
