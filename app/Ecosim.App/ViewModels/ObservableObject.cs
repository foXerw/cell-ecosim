using System.ComponentModel;
using System.Runtime.CompilerServices;

namespace Ecosim.App.ViewModels;

/// <summary>
/// Hand-written instead of pulled from a toolkit: binding plumbing is part of
/// what this project is practising (design §11.2).
/// </summary>
public abstract class ObservableObject : INotifyPropertyChanged
{
    public event PropertyChangedEventHandler? PropertyChanged;

    protected void OnPropertyChanged([CallerMemberName] string? name = null)
        => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));

    protected bool Set<T>(ref T field, T value, [CallerMemberName] string? name = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value)) return false;
        field = value;
        OnPropertyChanged(name);
        return true;
    }

    /// <summary>Raises a change notification for a dependent property.</summary>
    protected void RaiseDependent(string name) => OnPropertyChanged(name);
}
