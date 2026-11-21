using System.ComponentModel;
using System.Runtime.CompilerServices;

namespace Ecosim.Core;

/// <summary>
/// One slider bound to one engine parameter. Lives in Ecosim.Core rather than
/// the app so the "add a parameter, get a slider" claim is testable without a
/// display.
/// </summary>
public sealed class ParamSlider : INotifyPropertyChanged
{
    private readonly World _world;
    private double _value;
    private string? _error;

    public ParamSlider(World world, ParamDescription description)
    {
        _world = world;
        Description = description;
        _value = ReadOrFallback();
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    public ParamDescription Description { get; }

    public string Title => Description.Label;

    public string Section => Description.Section;

    public double Minimum => Description.Min;

    public double Maximum => Description.Max;

    public double Step => Description.Step;

    public double Default => Description.Default;

    /// <summary>Set by the engine when it refuses a value, cleared on success.
    /// The panel shows it inline instead of letting a bad drag throw.</summary>
    public string? Error
    {
        get => _error;
        private set
        {
            if (_error == value) return;
            _error = value;
            OnPropertyChanged();
        }
    }

    public double Value
    {
        get => _value;
        set
        {
            var clamped = Math.Clamp(value, Minimum, Maximum);
            var asInt = (int)Math.Round(clamped);
            if (asInt == (int)_value && Error is null) return;
            try
            {
                _world.SetParam(Description.Index, asInt);
                _value = asInt;
                Error = null;
            }
            catch (EcosimException ex)
            {
                Error = ex.Message;
            }
            OnPropertyChanged();
            OnPropertyChanged(nameof(Error));
        }
    }

    /// <summary>Re-reads the engine's value, e.g. after a world rebuild.</summary>
    public void Refresh()
    {
        _value = ReadOrFallback();
        OnPropertyChanged(nameof(Value));
    }

    private double ReadOrFallback()
    {
        try
        {
            return _world.GetParam(Description.Index);
        }
        catch (EcosimException)
        {
            return Description.Default;
        }
    }

    private void OnPropertyChanged([CallerMemberName] string? name = null)
        => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
}
