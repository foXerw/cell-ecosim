using System.Windows.Input;
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
    private long _generation;
    private double _stepMs;
    private bool _isRunning;
    private int _speed = 30;

    public MainViewModel()
    {
        _world = new World(WorldConfig.Default(DefaultWidth, DefaultHeight));
        _clock = new SimClock(_world, _speed);
        _clock.Tick += OnTick;

        ToggleRunCommand = new RelayCommand(ToggleRun);
        StepCommand = new RelayCommand(Step, () => !IsRunning);
    }

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

    public string StatusText
        => $"代 {Generation:N0} ｜ {_world.Width}×{_world.Height} ｜ 步进 {StepMs:F2} ms ｜ "
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
    }


    private void OnTick(object? sender, TickEventArgs e)
    {
        Generation = e.Generation;
        StepMs = e.StepMs;
    }

    public void Dispose()
    {
        _clock.Dispose();
        _world.Dispose();
    }
}
