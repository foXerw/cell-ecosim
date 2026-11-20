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

    /// <summary>Renders the current generation. Must be called on the UI thread:
    /// it locks the bitmap, and the world's lock keeps the step from racing it.</summary>
    public void Draw(World world, in Palette palette)
    {
        if (EnsureBitmap(world.Width, world.Height))
        {
            // A new bitmap means the Image.Source must be rebound by the caller.
        }
        using var locked = _bitmap!.Lock();
        world.RenderInto(locked.Address, locked.RowBytes, palette);
    }
}
