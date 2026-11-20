namespace Ecosim.Core;

/// <summary>
/// A reusable BGRA8 target for the engine's renderer. Reallocates only when the
/// size actually changes, because at 60 fps a per-frame allocation of 16 MB is
/// exactly the kind of garbage this project should not produce.
/// </summary>
public sealed class FrameBuffer
{
    public int Width { get; private set; }

    public int Height { get; private set; }

    /// <summary>Bytes per row, always a multiple of four.</summary>
    public int RowPitch { get; private set; }

    public byte[] Bytes { get; private set; } = Array.Empty<byte>();

    public int Reallocations { get; private set; }

    public void Ensure(int width, int height)
    {
        if (width <= 0 || height <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(width), "帧缓冲尺寸必须为正");
        }
        if (width == Width && height == Height) return;

        Width = width;
        Height = height;
        RowPitch = width * 4;
        Bytes = new byte[(long)RowPitch * height];
        ++Reallocations;
    }

    public Span<byte> AsSpan() => Bytes.AsSpan(0, RowPitch * Height);

    public Memory<byte> AsMemory() => Bytes.AsMemory(0, RowPitch * Height);
}
