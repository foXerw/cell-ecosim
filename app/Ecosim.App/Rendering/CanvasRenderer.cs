using Avalonia;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using Ecosim.Core;

namespace Ecosim.App.Rendering;

/// <summary>
/// Owns the one-pixel-per-cell bitmap the engine renders into. The bitmap is
/// then scaled by a render transform, so a frame costs exactly one native
/// render and zero managed copies (design appendix A.2).
/// </summary>
public sealed class CanvasRenderer
{
    private WriteableBitmap? _bitmap;

    public WriteableBitmap? Bitmap => _bitmap;

    public bool EnsureBitmap(int width, int height)
    {
        if (_bitmap is not null && _bitmap.PixelSize.Width == width &&
            _bitmap.PixelSize.Height == height)
        {
            return false;
        }
        _bitmap = new WriteableBitmap(new PixelSize(width, height), new Vector(96, 96),
                                      PixelFormat.Bgra8888);
        return true;
    }

    /// <summary>
    /// Paints an immutable snapshot. Takes no engine lock: the snapshot was
    /// copied by the clock thread right after its step, so a repaint can never
    /// queue behind a long simulation batch.
    /// </summary>
    public void DrawSnapshot(SpeciesSnapshot snapshot, in Palette palette)
    {
        EnsureBitmap(snapshot.Width, snapshot.Height);
        using var locked = _bitmap!.Lock();
        World.RenderSnapshotInto(snapshot, locked.Address, locked.RowBytes, palette);
    }
}
