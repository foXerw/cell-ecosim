using System.Windows.Input;
using Avalonia;
using Ecosim.Core;

namespace Ecosim.App.ViewModels;

public enum CanvasTool
{
    Draw,
    Erase,
    Pan,
}

/// <summary>
/// Everything about looking at and touching the grid: the viewport transform,
/// the current tool, and the pointer gestures that turn into paint ops or pans.
/// No Avalonia controls in here, so the maths stays testable.
/// </summary>
public sealed class CanvasViewModel : ObservableObject
{
    private readonly World _world;
    private Size _canvasSize = new(800, 600);
    private Viewport _viewport;
    private bool _viewportInitialised;

    private CanvasTool _tool = CanvasTool.Draw;
    private RenderLayer _layer = RenderLayer.Species;
    private int _brushRadius = 1;
    private int _species = 1;
    private (int X, int Y)? _hoveredCell;

    private bool _panning;
    private Point _lastPointer;
    private (int X, int Y)? _strokeFrom;

    public CanvasViewModel(World world)
    {
        _world = world;
        SelectToolCommand = new RelayCommand(p =>
        {
            if (p is CanvasTool tool) Tool = tool;
        });
        SelectLayerCommand = new RelayCommand(p =>
        {
            if (p is RenderLayer layer) Layer = layer;
        });
    }

    public ICommand SelectToolCommand { get; }

    public ICommand SelectLayerCommand { get; }

    /// <summary>What the canvas is coloured by. Heat layers also make the clock
    /// capture the extra planes.</summary>
    public RenderLayer Layer
    {
        get => _layer;
        set
        {
            if (!Set(ref _layer, value)) return;
            LayerChanged?.Invoke(this, EventArgs.Empty);
        }
    }

    /// <summary>The view model cannot reach the clock, so it raises this and
    /// MainViewModel turns CaptureHeat on or off.</summary>
    public event EventHandler? LayerChanged;

    public Size CanvasSize => _canvasSize;

    public Viewport Viewport
    {
        get => _viewport;
        private set
        {
            if (!Set(ref _viewport, value)) return;
            RaiseDependent(nameof(ZoomPercent));
            RaiseDependent(nameof(Destination));
            RaiseDependent(nameof(CanvasLeft));
            RaiseDependent(nameof(CanvasTop));
            RaiseDependent(nameof(CanvasWidth));
            RaiseDependent(nameof(CanvasHeight));
        }
    }

    public int ZoomPercent => (int)Math.Round(Viewport.CellSize * 100);

    /// <summary>Where the grid is drawn. The view lays the bitmap out from
    /// these four values instead of a render transform, so there is no implicit
    /// centering between what is drawn and what a click maps to.</summary>
    public ScreenRect Destination => Viewport.DestinationRect(_world.Width, _world.Height);

    public double CanvasLeft => Destination.X;

    public double CanvasTop => Destination.Y;

    public double CanvasWidth => Destination.Width;

    public double CanvasHeight => Destination.Height;

    public CanvasTool Tool
    {
        get => _tool;
        set => Set(ref _tool, value);
    }

    public int BrushRadius
    {
        get => _brushRadius;
        set => Set(ref _brushRadius, Math.Clamp(value, 1, 32));
    }

    public int Species
    {
        get => _species;
        set => Set(ref _species, Math.Clamp(value, 1, AbiConstants.MaxSpecies - 1));
    }

    public (int X, int Y)? HoveredCell
    {
        get => _hoveredCell;
        private set => Set(ref _hoveredCell, value);
    }

    public string HoverText => _hoveredCell is { } c
        ? $"格 ({c.X}, {c.Y})"
        : "—";

    public void SetCanvasSize(Size size)
    {
        _canvasSize = size;
        if (!_viewportInitialised && size.Width > 1 && size.Height > 1)
        {
            _viewportInitialised = true;
            Viewport = Viewport.Fit(_world.Width, _world.Height, size.Width, size.Height);
        }
    }

    public void Fit()
        => Viewport = Viewport.Fit(_world.Width, _world.Height, _canvasSize.Width,
                                   _canvasSize.Height);

    public void ZoomAt(Point screen, double factor)
        => Viewport = Viewport.ZoomAt(screen.X, screen.Y, factor);

    public (int X, int Y) CellAt(Point screen) => Viewport.ScreenToCell(screen.X, screen.Y);

    /// <summary>Begin a gesture. Returns the paint ops for the initial dab, or
    /// null when the gesture is a pan.</summary>
    public IReadOnlyList<PaintOp>? PointerPressed(Point screen, bool panRequested)
    {
        if (panRequested || _tool == CanvasTool.Pan)
        {
            _panning = true;
            _lastPointer = screen;
            return null;
        }

        var cell = CellAt(screen);
        _strokeFrom = cell;
        return BrushStroke.Line(cell, cell, ActiveSpecies, BrushRadius);
    }

    /// <summary>Continue a gesture. Paint ops for a stroke, null for a pan or a
    /// plain hover.</summary>
    public IReadOnlyList<PaintOp>? PointerMoved(Point screen, bool pressed)
    {
        if (_panning && pressed)
        {
            Viewport = Viewport.Panned(screen.X - _lastPointer.X, screen.Y - _lastPointer.Y);
            _lastPointer = screen;
            return null;
        }

        var cell = CellAt(screen);
        HoveredCell = cell;

        if (!pressed || _strokeFrom is null) return null;

        var ops = BrushStroke.Line(_strokeFrom.Value, cell, ActiveSpecies, BrushRadius);
        _strokeFrom = cell;
        return ops;
    }

    public void PointerReleased()
    {
        _panning = false;
        _strokeFrom = null;
    }

    private int ActiveSpecies => _tool == CanvasTool.Erase ? 0 : _species;
}
