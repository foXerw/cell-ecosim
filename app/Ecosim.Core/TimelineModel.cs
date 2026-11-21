using System.ComponentModel;
using System.Runtime.CompilerServices;

namespace Ecosim.Core;

/// <summary>
/// A view over the engine's snapshot ring: it holds no board of its own
/// (appendix A.3), only an index into what the engine already kept.
/// </summary>
public sealed class TimelineModel : INotifyPropertyChanged
{
    private readonly World _world;
    private int _index = -1;

    public TimelineModel(World world)
    {
        _world = world ?? throw new ArgumentNullException(nameof(world));
        Refresh();
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    public int Count { get; private set; }

    /// <summary>-1 means "live, not reviewing".</summary>
    public int Index
    {
        get => _index;
        set
        {
            var clamped = Math.Clamp(value, -1, Math.Max(-1, Count - 1));
            if (clamped == _index) return;
            _index = clamped;
            OnPropertyChanged();
            OnPropertyChanged(nameof(IsReviewing));
            OnPropertyChanged(nameof(CurrentGeneration));
            OnPropertyChanged(nameof(ScrubValue));
        }
    }

    public bool IsReviewing => Index >= 0;

    /// <summary>What the scrubber shows. Live is a real value (0) rather than
    /// the index of whatever was reviewed last: the slider binds to this OneWay
    /// and would otherwise park its thumb mid-track while the board runs.</summary>
    public int ScrubValue => IsReviewing ? Index : 0;

    /// <summary>For a slider bound to Maximum: an empty ring still needs a
    /// valid (if useless) range rather than a negative bound.</summary>
    public int MaximumIndex => Math.Max(0, Count - 1);

    public long CurrentGeneration => Index >= 0 ? _world.SnapshotGeneration(Index) : _world.Generation;

    public void Refresh()
    {
        Count = _world.SnapshotCount();
        var wasReviewing = IsReviewing;
        if (_index >= Count) _index = Count - 1;

        OnPropertyChanged(nameof(Count));
        // Every derived reading is announced, not just the ones that happened
        // to change: MaximumIndex is what the slider's Maximum binds to, so a
        // Refresh that grew the ring but stayed quiet left the scrubber with a
        // zero-length track no matter how long the simulation ran.
        OnPropertyChanged(nameof(MaximumIndex));
        OnPropertyChanged(nameof(Index));
        OnPropertyChanged(nameof(CurrentGeneration));
        OnPropertyChanged(nameof(ScrubValue));
        if (wasReviewing != IsReviewing) OnPropertyChanged(nameof(IsReviewing));
    }

    /// <summary>Jumps to a snapshot; the engine rewinds the generation with it,
    /// so replaying forward afterwards reproduces the same board.</summary>
    public bool Restore(int index)
    {
        if (index < 0 || index >= Count) return false;
        _world.SnapshotRestore(index);
        Index = index;
        return true;
    }

    /// <summary>Returns to the newest snapshot, or to -1 (live) if none exists.</summary>
    public void ExitReview()
    {
        Index = -1;
    }

    private void OnPropertyChanged([CallerMemberName] string? name = null)
        => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
}
