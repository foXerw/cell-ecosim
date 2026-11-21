using System.Windows.Input;
using Avalonia.Media.Imaging;
using Avalonia.Platform.Storage;
using Ecosim.App.Rendering;
using Ecosim.Core;

namespace Ecosim.App.ViewModels;

/// <summary>
/// Owns the world, the clock and everything the chrome binds to. The canvas
/// itself lives in <see cref="CanvasViewModel"/>; this type only coordinates.
/// </summary>
public sealed class MainViewModel : ObservableObject, IDisposable
{
    public const int DefaultWidth = 512;
    public const int DefaultHeight = 512;

    private World _world;
    private SimClock _clock;
    private readonly Avalonia.Threading.DispatcherTimer _pump;
    private readonly CanvasRenderer _renderer = new();
    private CanvasViewModel _canvas;
    private WriteableBitmap? _canvasBitmap;

    private int _alive;
    private long _generation;
    private long _paintedGeneration = -1;
    private double _stepMs;
    private bool _isRunning;
    private int _speed = 60;
    private string _birthText = "3";
    private string _surviveText = "23";
    private int _newWidth = DefaultWidth;
    private int _newHeight = DefaultHeight;
    private string? _notice;
    private Pattern? _selectedPattern;

    public MainViewModel()
    {
        _world = CreateWorld(DefaultWidth, DefaultHeight, WorldConfig.Default(DefaultWidth, DefaultHeight));
        _canvas = new CanvasViewModel(_world);
        Params = new ParamsModel(_world);
        _clock = CreateClock();

        // The display runs on its own beat. Coupling repaints to simulation
        // ticks made the animation inherit the OS timer grid (~45 ms here) and
        // look like a slideshow; the pump paints at frame rate while the clock
        // decides how many generations fit into each frame.
        _pump = new Avalonia.Threading.DispatcherTimer(Avalonia.Threading.DispatcherPriority.Render)
        {
            Interval = TimeSpan.FromMilliseconds(8),
        };
        // Poll often, paint only when something actually changed: repainting an
        // unchanged generation costs a texture upload and buys nothing.
        _pump.Tick += (_, _) =>
        {
            var snap = _clock.LatestSnapshot;
            if (snap is not null && snap.Generation != _paintedGeneration) PaintSnapshot(snap);
        };

        ToggleRunCommand = new RelayCommand(ToggleRun);
        StepCommand = new RelayCommand(Step, () => !IsRunning);
        ZoomInCommand = new RelayCommand(() => ZoomAtCentre(1.5));
        ZoomOutCommand = new RelayCommand(() => ZoomAtCentre(1 / 1.5));
        FitViewCommand = new RelayCommand(() =>
        {
            _canvas.Fit();
            Redraw();
        });
        ClearCommand = new RelayCommand(() =>
        {
            _world.Clear();
            Redraw();
        });
        RandomizeCommand = new RelayCommand(() =>
        {
            _world.Randomize(new SeedConfig
            {
                Species = _canvas.Species,
                DensityPermille = 220,
                Seed = unchecked((ulong)Environment.TickCount64) | 1,
            });
            Redraw();
        });
        ApplyPatternCommand = new RelayCommand(ApplySelectedPattern, () => _selectedPattern is not null);
        ApplyRuleCommand = new RelayCommand(ApplyRule);
        NewWorldCommand = new RelayCommand(NewWorld);
        SaveRleCommand = new AsyncRelayCommand(_ => SaveRleAsync());
        OpenRleCommand = new AsyncRelayCommand(_ => OpenRleAsync());
        ToggleTopologyCommand = new RelayCommand(() =>
        {
            Topology = Topology == GridTopology.Torus ? GridTopology.Bounded : GridTopology.Torus;
            OnPropertyChanged(nameof(TopologyLabel));
        });
    }

    // ---- state -----------------------------------------------------------

    public World World => _world;

    public CanvasViewModel Canvas => _canvas;

    /// <summary>Sliders generated from the engine's parameter table.</summary>
    public ParamsModel Params { get; private set; }

    public WriteableBitmap? CanvasBitmap => _canvasBitmap;

    public Palette Palette { get; } = Palette.Default;

    public IReadOnlyList<Pattern> Patterns { get; } = PatternLibrary.All;

    public Pattern? SelectedPattern
    {
        get => _selectedPattern;
        set
        {
            if (Set(ref _selectedPattern, value))
            {
                ((RelayCommand)ApplyPatternCommand).NotifyCanExecuteChanged();
            }
        }
    }

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

    public int Alive
    {
        get => _alive;
        private set
        {
            if (Set(ref _alive, value)) RaiseDependent(nameof(StatusText));
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

    public string BirthText
    {
        get => _birthText;
        set => Set(ref _birthText, value);
    }

    public string SurviveText
    {
        get => _surviveText;
        set => Set(ref _surviveText, value);
    }

    public int NewWidth
    {
        get => _newWidth;
        set => Set(ref _newWidth, Math.Clamp(value, 1, AbiConstants.MaxDim));
    }

    public int NewHeight
    {
        get => _newHeight;
        set => Set(ref _newHeight, Math.Clamp(value, 1, AbiConstants.MaxDim));
    }

    /// <summary>Transient message for the status bar (imports, rebuilds, gestures).
    /// The view writes it too, so the setter is internal rather than private.</summary>
    public string? Notice
    {
        get => _notice;
        internal set => Set(ref _notice, value);
    }

    public GridTopology Topology
    {
        get => _world.Topology;
        set
        {
            _world.Topology = value;  // hot switch: state survives (appendix A.1)
            OnPropertyChanged();
            RaiseDependent(nameof(StatusText));
        }
    }

    public string TopologyLabel => Topology == GridTopology.Torus ? "环形" : "有界";

    public ICommand ToggleTopologyCommand { get; private set; } = null!;

    public string StatusText
        => $"代 {Generation:N0} ｜ 存活 {Alive:N0} ｜ {_world.Width}×{_world.Height} ｜ "
         + $"步进 {StepMs:F2} ms ｜ "
         + (IsRunning ? $"播放中 {Speed:N0} 代/秒" : "已暂停")
;

    // ---- commands --------------------------------------------------------

    public ICommand ToggleRunCommand { get; }

    public ICommand StepCommand { get; }

    public ICommand ZoomInCommand { get; }

    public ICommand ZoomOutCommand { get; }

    public ICommand FitViewCommand { get; }

    public ICommand ClearCommand { get; }

    public ICommand RandomizeCommand { get; }

    public ICommand ApplyPatternCommand { get; }

    public ICommand ApplyRuleCommand { get; }

    public ICommand NewWorldCommand { get; }

    public ICommand SaveRleCommand { get; }

    public ICommand OpenRleCommand { get; }

    /// <summary>Storage root for the save/open dialogs; the view sets it.</summary>
    public Func<Task<IStorageProvider?>> StorageProviderFactory { get; set; } =
        () => Task.FromResult<IStorageProvider?>(null);

    // ---- behaviour -------------------------------------------------------

    /// <summary>Raised after the bitmap's pixels changed. The view must
    /// invalidate the Image visual: mutating a WriteableBitmap does not, by
    /// itself, mark the visual dirty, so the compositor keeps showing the
    /// previously uploaded texture (that is exactly the "frozen until you drag"
    /// bug this event exists to prevent).</summary>
    public event EventHandler? FramePainted;

    /// <summary>Captures the board and paints it. Used for immediate feedback
    /// while paused (painting, stepping, imports).</summary>
    public void Redraw() => PaintSnapshot(_world.Capture());

    private void PaintSnapshot(SpeciesSnapshot snapshot)
    {
        _paintedGeneration = snapshot.Generation;
        Alive = snapshot.Alive;
        Generation = snapshot.Generation;
        _renderer.DrawSnapshot(snapshot, Palette);
        if (!ReferenceEquals(_renderer.Bitmap, _canvasBitmap))
        {
            _canvasBitmap = _renderer.Bitmap;
            OnPropertyChanged(nameof(CanvasBitmap));
        }
        FramePainted?.Invoke(this, EventArgs.Empty);
    }

    public void ZoomAtCentre(double factor)
    {
        var size = _canvas.CanvasSize;
        _canvas.ZoomAt(new Avalonia.Point(size.Width / 2, size.Height / 2), factor);
    }

    public void ZoomAt(Avalonia.Point screen, double factor) => _canvas.ZoomAt(screen, factor);

    private void ToggleRun()
    {
        if (_clock.IsRunning)
        {
            _clock.Stop();
            _pump.Stop();
            IsRunning = false;
            Redraw();
        }
        else
        {
            _clock.Start();
            _pump.Start();
            IsRunning = true;
        }
    }

    private void Step()
    {
        _world.Step(1);
        Generation = _world.Generation;
        Redraw();
    }

    private void ApplySelectedPattern()
    {
        if (_selectedPattern is null) return;
        var doc = RleCodec.Decode(_selectedPattern.Rle);
        var x = (_world.Width - doc.Width) / 2;
        var y = (_world.Height - doc.Height) / 2;
        _world.FromRleAt(_selectedPattern.Rle, Math.Max(x, 0), Math.Max(y, 0));
        Notice = $"已放置图案「{_selectedPattern.Name}」于 ({Math.Max(x, 0)}, {Math.Max(y, 0)})";
        Redraw();
    }

    private void ApplyRule()
    {
        if (!TryParseMask(BirthText, out var birth) || !TryParseMask(SurviveText, out var survive))
        {
            Notice = "规则掩码只能是 0..8 的数字串，例如 B=3、S=23";
            return;
        }

        // Masks live in EcosimConfig, so a rule change rebuilds the world; the
        // current cells travel along as RLE.
        var snapshot = _world.ToRle();
        var config = WorldConfig.Default(_world.Width, _world.Height) with
        {
            BirthMask = birth,
            SurviveMask = survive,
            Topology = _world.Topology,
        };
        Rebuild(config);
        _world.FromRle(snapshot);
        Notice = $"规则已切换为 B{BirthText}/S{SurviveText}";
        Redraw();
    }

    private void NewWorld()
    {
        var config = WorldConfig.Default(NewWidth, NewHeight) with
        {
            BirthMask = ParseMaskOrLife(BirthText),
            SurviveMask = ParseMaskOrLife(SurviveText, isSurvive: true),
        };
        Rebuild(config);
        _world.Randomize(new SeedConfig
        {
            Species = 1,
            DensityPermille = 220,
            Seed = unchecked((ulong)Environment.TickCount64) | 1,
        });
        Notice = $"已新建 {NewWidth}×{NewHeight} 世界";
        _canvas.Fit();
        Redraw();
    }

    private void Rebuild(WorldConfig config)
    {
        var wasRunning = _clock.IsRunning;
        _clock.Stop();
        _clock.Dispose();
        _world.Dispose();

        _world = CreateWorld(config.Width, config.Height, config);
        _canvas = new CanvasViewModel(_world);
        Params = new ParamsModel(_world);
        OnPropertyChanged(nameof(Canvas));
        OnPropertyChanged(nameof(Params));
        _clock = CreateClock();
        if (wasRunning)
        {
            _clock.Start();
            _pump.Start();
        }
        else
        {
            _pump.Stop();
        }
        IsRunning = wasRunning;
        Generation = 0;
    }

    private World CreateWorld(int width, int height, WorldConfig config)
    {
        var world = new World(config with { Width = width, Height = height });
        world.Randomize(new SeedConfig
        {
            Species = 1,
            DensityPermille = 220,
            Seed = unchecked((ulong)Environment.TickCount64) | 1,
        });
        return world;
    }

    private SimClock CreateClock()
    {
        var clock = new SimClock(_world, _speed);
        clock.Tick += OnTick;
        return clock;
    }

    private async Task SaveRleAsync()
    {
        var provider = await StorageProviderFactory().ConfigureAwait(true);
        if (provider is null) return;
        var file = await provider.SaveFilePickerAsync(new FilePickerSaveOptions
        {
            Title = "导出 RLE",
            SuggestedFileName = "ecosim.rle",
        }).ConfigureAwait(true);
        if (file is null) return;

        await using var stream = await file.OpenWriteAsync().ConfigureAwait(true);
        await using var writer = new StreamWriter(stream);
        await writer.WriteAsync(_world.ToRle()).ConfigureAwait(true);
        Notice = $"已导出到 {file.Name}";
    }

    private async Task OpenRleAsync()
    {
        var provider = await StorageProviderFactory().ConfigureAwait(true);
        if (provider is null) return;
        var files = await provider.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = "导入 RLE",
            AllowMultiple = false,
        }).ConfigureAwait(true);
        if (files.Count == 0) return;

        await using var stream = await files[0].OpenReadAsync().ConfigureAwait(true);
        using var reader = new StreamReader(stream);
        var text = await reader.ReadToEndAsync().ConfigureAwait(true);

        try
        {
            var doc = RleCodec.Decode(text);
            if (doc.Width > _world.Width || doc.Height > _world.Height)
            {
                // Appendix A.4.3: rebuild at the saved size instead of clipping.
                var config = WorldConfig.Default(Math.Max(doc.Width, 64), Math.Max(doc.Height, 64)) with
                {
                    BirthMask = ParseMaskOrLife(BirthText),
                    SurviveMask = ParseMaskOrLife(SurviveText, isSurvive: true),
                };
                Rebuild(config);
                _world.Clear();
                Notice = $"已按存档尺寸重建 {_world.Width}×{_world.Height}";
            }
            _world.FromRle(text);
            _canvas.Fit();
            Redraw();
        }
        catch (FormatException ex)
        {
            Notice = $"RLE 解析失败：{ex.Message}";
        }
    }

    private static bool TryParseMask(string text, out byte[] mask)
    {
        mask = new byte[9];
        var trimmed = text.Trim();
        if (trimmed.Length == 0) return false;
        foreach (var c in trimmed)
        {
            if (c < '0' || c > '8') return false;
            mask[c - '0'] = 1;
        }
        return true;
    }

    private static byte[] ParseMaskOrLife(string text, bool isSurvive = false)
        => TryParseMask(text, out var mask)
            ? mask
            : (isSurvive ? new byte[] { 0, 0, 1, 1, 0, 0, 0, 0, 0 }
                         : new byte[] { 0, 0, 0, 1, 0, 0, 0, 0, 0 });

    private void OnTick(object? sender, TickEventArgs e)
    {
        // Numbers only: the render pump paints. Painting here would tie the
        // display rate to the simulation rate.
        Generation = e.Generation;
        StepMs = e.StepMs;
    }

    public void Dispose()
    {
        _pump.Stop();
        _clock.Dispose();
        _world.Dispose();
    }
}
