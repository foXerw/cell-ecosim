using System.Windows.Input;
using Avalonia.Media.Imaging;
using Ecosim.App.Rendering;
using Ecosim.Core;

namespace Ecosim.App.ViewModels;

/// <summary>
/// Owns the world and the clock. Milestone M0 scope: create a world, run it,
/// show the cost. The canvas, brush and panels arrive in later tasks and hang
/// off this same object.
/// </summary>
public sealed class MainViewModel : ObservableObject, IDisposable
{
    public const int DefaultWidth = 512;
    public const int DefaultHeight = 512;

    private readonly World _world;
    private readonly SimClock _clock;
    private readonly CanvasViewModel _canvas;
    private readonly CanvasRenderer _renderer;
    private WriteableBitmap? _canvasBitmap;
    private int _alive;
    private long _generation;
    private double _stepMs;
    private bool _isRunning;
    private int _speed = 30;

    public MainViewModel()
    {
        _world = new World(WorldConfig.Default(DefaultWidth, DefaultHeight));
        // Open on a live soup rather than an empty board: the first thing the
        // user sees should already be moving.
        _world.Randomize(new SeedConfig
        {
            Species = 1,
            DensityPermille = 220,
            Seed = unchecked((ulong)Environment.TickCount64) | 1,
        });
        _clock = new SimClock(_world, _speed);
        _clock.Tick += OnTick;
        _canvas = new CanvasViewModel(_world);
        _renderer = new CanvasRenderer();

        ToggleRunCommand = new RelayCommand(ToggleRun);
        StepCommand = new RelayCommand(Step, () => !IsRunning);
        ZoomInCommand = new RelayCommand(() => ZoomAtCentre(1.5));
        ZoomOutCommand = new RelayCommand(() => ZoomAtCentre(1 / 1.5));
        FitViewCommand = new RelayCommand(() =>
        {
            _canvas.Fit();
            Redraw();
        });
    }

    public CanvasViewModel Canvas => _canvas;

    /// <summary>The bitmap the Image binds to; changes only when the world size does.</summary>
    public WriteableBitmap? CanvasBitmap => _canvasBitmap;

    public Palette Palette { get; } = Palette.Default;

    public ICommand ZoomInCommand { get; }

    public ICommand ZoomOutCommand { get; }

    public ICommand FitViewCommand { get; }

    /// <summary>One native render into the bitmap. UI thread only.</summary>
    public void Redraw()
    {
        Alive = _world.Stats().Alive;
        _renderer.Draw(_world, Palette);
        if (!ReferenceEquals(_renderer.Bitmap, _canvasBitmap))
        {
            _canvasBitmap = _renderer.Bitmap;
            OnPropertyChanged(nameof(CanvasBitmap));
        }
    }

    public void ZoomAtCentre(double factor)
    {
        var size = _canvas.CanvasSize;
        _canvas.ZoomAt(new Avalonia.Point(size.Width / 2, size.Height / 2), factor);
    }

    public void ZoomAt(Avalonia.Point screen, double factor) => _canvas.ZoomAt(screen, factor);

    public string Title => "Cell Ecosim — 细胞生态模拟器";

    public ICommand ToggleRunCommand { get; }

    public ICommand StepCommand { get; }

    public World World => _world;

    public bool IsRunning
    {
        get => _isRunning;
        private set
        {
            if (Set(ref _isRunning, value))
            {
                RaiseDependent(nameof(StatusText));
                ((RelayCommand)StepCommand).NotifyCanExecuteChanged();
            }
        }
    }

    public long Generation
    {
        get => _generation;
        private set
        {
            if (Set(ref _generation, value)) RaiseDependent(nameof(StatusText));
        }
    }

    public double StepMs
    {
        get => _stepMs;
        private set
        {
            if (Set(ref _stepMs, value)) RaiseDependent(nameof(StatusText));
        }
    }

    public int Speed
    {
        get => _speed;
        set
        {
            var clamped = Math.Clamp(value, 1, 100_000);
            if (Set(ref _speed, clamped))
            {
                _clock.SetSpeed(clamped);
                RaiseDependent(nameof(StatusText));
            }
        }
    }

    public int Width => _world.Width;

    public int Height => _world.Height;

    public int Alive
    {
        get => _alive;
        private set
        {
            if (Set(ref _alive, value)) RaiseDependent(nameof(StatusText));
        }
    }

    public string StatusText
        => $"代 {Generation:N0} ｜ 存活 {Alive:N0} ｜ {_world.Width}×{_world.Height} ｜ "
         + $"步进 {StepMs:F2} ms ｜ "
         + (IsRunning ? $"播放中 {Speed:N0} 代/秒" : "已暂停");

    private void ToggleRun()
    {
        if (_clock.IsRunning)
        {
            _clock.Stop();
            IsRunning = false;
        }
        else
        {
            _clock.Start();
            IsRunning = true;
        }
    }

    private void Step()
    {
        _world.Step(1);
        Generation = _world.Generation;
        Redraw();
    }


    private void OnTick(object? sender, TickEventArgs e)
    {
        Generation = e.Generation;
        StepMs = e.StepMs;
        Redraw();  // already on the UI thread: the clock captured its context
    }

    public void Dispose()
    {
        _clock.Dispose();
        _world.Dispose();
    }
}
