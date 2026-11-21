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
    private readonly Avalonia.Threading.DispatcherTimer _chartTimer;
    private readonly CanvasRenderer _renderer = new();
    private CanvasViewModel _canvas;
    private WriteableBitmap? _canvasBitmap;

    private bool _reviewing;
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
        _world = CreateFresh(StartupConfig());
        Timeline = new TimelineModel(_world);
        _canvas = new CanvasViewModel(_world);
        _canvas.LayerChanged += (_, _) => SyncLayer();
        Params = new ParamsModel(_world);
        Timeline = new TimelineModel(_world);
        _reviewing = false;
        OnPropertyChanged(nameof(Timeline));
        RaiseDependent(nameof(ReviewLabel));
        _clock = CreateClock();
        ApplyStartupLayer();

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
            if (_reviewing) return;
            var snap = _clock.LatestSnapshot;
            if (snap is not null && snap.Generation != _paintedGeneration) PaintSnapshot(snap);
        };

        // Reading 600 samples across the border every frame would be pure
        // waste, so the chart refreshes on its own slower beat.
        _chartTimer = new Avalonia.Threading.DispatcherTimer(
            Avalonia.Threading.DispatcherPriority.Background)
        {
            Interval = TimeSpan.FromMilliseconds(250),
        };
        _chartTimer.Tick += (_, _) => RefreshHistory();
        _chartTimer.Start();

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
        SaveCewCommand = new AsyncRelayCommand(_ => SaveCewAsync());
        OpenCewCommand = new AsyncRelayCommand(_ => OpenCewAsync());
        ExitReviewCommand = new RelayCommand(ExitReview);
        ToggleTopologyCommand = new RelayCommand(() =>
        {
            Topology = Topology == GridTopology.Torus ? GridTopology.Bounded : GridTopology.Torus;
            OnPropertyChanged(nameof(TopologyLabel));
        });
    }

    // ---- state -----------------------------------------------------------

    public World World => _world;

    public CanvasViewModel Canvas => _canvas;

    /// <summary>View over the engine's snapshot ring (it stores nothing).</summary>
    public TimelineModel Timeline { get; private set; }

    public bool IsReviewing => Timeline.IsReviewing;

    /// <summary>Dragging the scrubber pauses the clock first: reviewing a
    /// history frame while the simulation keeps stepping would overwrite it.</summary>
    public double ScrubIndex
    {
        get => Math.Max(0, Timeline.Index);
        set => ScrubTo((int)Math.Round(value));
    }

    public ICommand ExitReviewCommand { get; private set; } = null!;

    private void ScrubTo(int index)
    {
        if (!_reviewing && _clock.IsRunning)
        {
            _clock.Stop();
            _pump.Stop();
            IsRunning = false;
        }
        _reviewing = true;
        if (!Timeline.Restore(index)) return;
        Redraw();
        RaiseDependent(nameof(ScrubIndex));
        RaiseDependent(nameof(IsReviewing));
        RaiseDependent(nameof(ReviewLabel));
    }

    private void ExitReview()
    {
        _reviewing = false;
        Timeline.ExitReview();
        Redraw();
        Notice = "已退出回放，回到当前状态";
        RaiseDependent(nameof(ScrubIndex));
        RaiseDependent(nameof(IsReviewing));
        RaiseDependent(nameof(ReviewLabel));
    }

    public string ReviewLabel => Timeline.IsReviewing
        ? $"回放中：第 {Timeline.CurrentGeneration:N0} 代"
        : "实时";

    /// <summary>Sliders generated from the engine's parameter table.</summary>
    public ParamsModel Params { get; private set; }

    public WriteableBitmap? CanvasBitmap => _canvasBitmap;

    /// <summary>Colours plus the layer the canvas asked for; the heat layers
    /// need the clock to capture more planes, so the two stay in step here.</summary>
    public Palette Palette { get; private set; } = Palette.Default;

    /// <summary>ECOSIM_LAYER=species|energy|resource|age, so a heat map can be
    /// reproduced from the command line rather than clicked into existence.</summary>
    private void ApplyStartupLayer()
    {
        var layer = Environment.GetEnvironmentVariable("ECOSIM_LAYER");
        if (string.IsNullOrWhiteSpace(layer)) return;
        if (Enum.TryParse<RenderLayer>(layer, ignoreCase: true, out var parsed))
        {
            _canvas.Layer = parsed;
        }
    }

    private void SyncLayer()
    {
        Palette = Palette.Default.WithLayer(_canvas.Layer);
        _clock.CaptureHeat = Palette.NeedsHeatPlanes;
        Redraw();
    }

    public IReadOnlyList<Pattern> Patterns { get; } = PatternLibrary.All;

    /// <summary>Rule modes are not hot-switchable inside the engine (they mean
    /// different things to the same cells), so switching rebuilds the world and
    /// carries the living cells across.</summary>
    public IReadOnlyList<string> ModeNames { get; } = new[] { "生命游戏（B/S）", "细胞生态" };

    public int ModeIndex
    {
        get => _world.Config.Mode == RuleMode.Ecosystem ? 1 : 0;
        set
        {
            var mode = value == 1 ? RuleMode.Ecosystem : RuleMode.BirthSurvive;
            if (mode == _world.Config.Mode) return;
            var carry = _world.Capture();
            Rebuild(_world.Config with { Mode = mode }, carry);
            Notice = value == 1
                ? "已切换到生态模式：细胞按各自物种的初始能量重新开始"
                : "已切换到生命游戏模式";
            OnPropertyChanged();
            Redraw();
        }
    }

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

    public ICommand SaveCewCommand { get; }

    public ICommand OpenCewCommand { get; }

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
    /// while paused (painting, stepping, imports). The capture has to match the
    /// palette: asking for a heat map out of a thin snapshot is a bug here, not
    /// a reason to bother the user.</summary>
    /// <summary>The population history the chart binds to.</summary>
    public IReadOnlyList<HistorySample>? HistorySamples { get; private set; }

    /// <summary>Newest first, capped: an event log nobody can read is noise.</summary>
    public System.Collections.ObjectModel.ObservableCollection<string> EventLines { get; } = new();

    private const int MaxEventLines = 100;

    /// <summary>Energy distribution, recomputed on the same slow beat.</summary>
    public int[]? Histogram { get; private set; }

    public void RefreshHistory()
    {
        HistorySamples = _world.History(600);
        OnPropertyChanged(nameof(HistorySamples));

        // The engine already counted energies during the census; reading twelve
        // integers is not the same bargain as copying four planes.
        Histogram = _world.EnergyHistogram(12);
        OnPropertyChanged(nameof(Histogram));

        var fresh = _world.PollEvents();
        if (fresh.Count == 0) return;
        for (var i = fresh.Count - 1; i >= 0; --i)
        {
            EventLines.Insert(0, fresh[i].Describe(SpeciesName(fresh[i].Species)));
        }
        while (EventLines.Count > MaxEventLines)
        {
            EventLines.RemoveAt(EventLines.Count - 1);
        }
    }

    private static string SpeciesName(int species) => species switch
    {
        1 => "藻",
        2 => "虫",
        3 => "兽",
        _ => $"物种 {species}",
    };

    public void Redraw() => PaintSnapshot(_world.CaptureFor(Palette));

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

    private void Rebuild(WorldConfig config, SpeciesSnapshot? carry = null)
    {
        var wasRunning = _clock.IsRunning;
        _clock.Stop();
        _clock.Dispose();
        _world.Dispose();

        _world = carry is null ? CreateFresh(config) : CreateCarried(config, carry);
        _canvas = new CanvasViewModel(_world);
        _canvas.LayerChanged += (_, _) => SyncLayer();
        Params = new ParamsModel(_world);
        Timeline = new TimelineModel(_world);
        _reviewing = false;
        OnPropertyChanged(nameof(Timeline));
        RaiseDependent(nameof(ReviewLabel));
        OnPropertyChanged(nameof(Canvas));
        OnPropertyChanged(nameof(Params));
        RefreshHistory();
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

    /// <summary>
    /// Startup configuration, with two environment overrides so a demo, a
    /// screenshot or a bug report can be reproduced exactly:
    ///   ECOSIM_MODE=eco    starts in ecosystem mode
    ///   ECOSIM_SEED=&lt;n&gt;    fixes the opening soup's seed
    /// </summary>
    private static WorldConfig StartupConfig()
    {
        var config = WorldConfig.Default(DefaultWidth, DefaultHeight);
        if (string.Equals(Environment.GetEnvironmentVariable("ECOSIM_MODE"), "eco",
                          StringComparison.OrdinalIgnoreCase))
        {
            config = config with { Mode = RuleMode.Ecosystem };
        }
        var seed = Environment.GetEnvironmentVariable("ECOSIM_SEED");
        if (ulong.TryParse(seed, out var parsed) && parsed != 0)
        {
            config = config with { Seed = parsed };
        }
        return config;
    }

    private static World CreateFresh(WorldConfig config)
    {
        var world = new World(config);
        if (config.Mode == RuleMode.Ecosystem)
        {
            SeedFoodWeb(world, config.Seed);
        }
        else
        {
            world.Randomize(new SeedConfig
            {
                Species = 1,
                DensityPermille = 220,
                Seed = config.Seed != 0 ? config.Seed
                                        : unchecked((ulong)Environment.TickCount64) | 1,
            });
        }
        return world;
    }

    /// <summary>
    /// A three-species opening. randomize() fills one species at a time, so the
    /// mix is assembled here and handed to the bulk loader, which reseeds each
    /// cell's energy from the species table.
    /// </summary>
    private static void SeedFoodWeb(World world, ulong seed)
    {
        var rng = new Random(unchecked((int)(seed != 0 ? seed
                                                       : (ulong)Environment.TickCount64)));
        var plane = new byte[world.Width * world.Height];
        for (var i = 0; i < plane.Length; ++i)
        {
            var roll = rng.Next(1000);
            plane[i] = roll switch
            {
                < 250 => 1,   // algae
                < 290 => 2,   // grazers
                < 296 => 3,   // hunters
                _ => 0,
            };
        }
        world.LoadSpecies(plane);
    }

    /// <summary>Rebuilds with the same living cells. RLE cannot do this job
    /// (it is single-state), so the species plane travels through the bulk
    /// loader instead - see design appendix A.6.</summary>
    private static World CreateCarried(WorldConfig config, SpeciesSnapshot carry)
    {
        var world = new World(config);
        if (carry.Width == config.Width && carry.Height == config.Height)
        {
            world.LoadSpecies(carry.Species);
        }
        else
        {
            world.FromRle(EncodeCarry(carry));
        }
        return world;
    }

    private static string EncodeCarry(SpeciesSnapshot carry)
        => RleCodec.Encode(carry.Width, carry.Height,
                            Enumerable.Range(0, carry.Species.Length)
                                      .Where(i => carry.Species[i] != 0)
                                      .Select(i => (X: i % carry.Width, Y: i / carry.Width)));

    private SimClock CreateClock()
    {
        var clock = new SimClock(_world, _speed);
        clock.Tick += OnTick;
        return clock;
    }

    private async Task SaveCewAsync()
    {
        var provider = await StorageProviderFactory().ConfigureAwait(true);
        if (provider is null) return;
        var file = await provider.SaveFilePickerAsync(new FilePickerSaveOptions
        {
            Title = "保存世界（.cew）",
            SuggestedFileName = "ecosim.cew",
        }).ConfigureAwait(true);
        if (file is null) return;

        await using var stream = await file.OpenWriteAsync().ConfigureAwait(true);
        await stream.WriteAsync(_world.SaveCew()).ConfigureAwait(true);
        Notice = $"已保存到 {file.Name}（含物种/能量/resource 与参数）";
    }

    private async Task OpenCewAsync()
    {
        var provider = await StorageProviderFactory().ConfigureAwait(true);
        if (provider is null) return;
        var files = await provider.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = "打开世界（.cew）",
            AllowMultiple = false,
        }).ConfigureAwait(true);
        if (files.Count == 0) return;

        await using var stream = await files[0].OpenReadAsync().ConfigureAwait(true);
        using var memory = new MemoryStream();
        await stream.CopyToAsync(memory).ConfigureAwait(true);

        try
        {
            _world.LoadCew(memory.ToArray());
            Notice = $"已读取 {files[0].Name}，回到第 {_world.Generation:N0} 代";
        }
        catch (EcosimException ex)
        {
            Notice = $"读取失败：{ex.Message}";
        }
        RefreshHistory();
        Redraw();
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
        Notice = _world.Config.Mode == RuleMode.Ecosystem
            ? $"已导出到 {file.Name}（注意：RLE 是单状态格式，物种与能量信息不在其中，" +
              "请用 .cew 保存生态）"
            : $"已导出到 {file.Name}";
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
        _chartTimer.Stop();
        _pump.Stop();
        _clock.Dispose();
        _world.Dispose();
    }
}
