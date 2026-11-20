using System.Runtime.InteropServices;
using Ecosim.Core;

namespace Ecosim.Tests;

public class RenderTests
{
    private static World TwoByTwoWithSpecies()
    {
        var world = new World(WorldConfig.Default(2, 2));
        world.Paint(new[]
        {
            new PaintOp(1, 0, 2, 1, 1),
            new PaintOp(0, 1, 1, 2, 2),
        });
        return world;
    }

    private static uint Pixel(FrameBuffer buffer, int x, int y)
        => BitConverter.ToUInt32(buffer.Bytes, y * buffer.RowPitch + x * 4);

    [Fact]
    public void Render_frame_paints_each_species_in_its_slot_colour()
    {
        using var world = TwoByTwoWithSpecies();
        var buffer = new FrameBuffer();
        world.RenderFrame(buffer, Palette.Default);

        Assert.Equal(2, buffer.Width);
        Assert.Equal(8, buffer.RowPitch);
        Assert.Equal(Palette.Default[0], Pixel(buffer, 0, 0));
        Assert.Equal(Palette.Default[1], Pixel(buffer, 1, 0));
        Assert.Equal(Palette.Default[2], Pixel(buffer, 0, 1));
        Assert.Equal(Palette.Default[0], Pixel(buffer, 1, 1));
    }

    [Fact]
    public void Frame_buffer_reallocates_only_when_the_size_changes()
    {
        var buffer = new FrameBuffer();
        buffer.Ensure(64, 64);
        buffer.Ensure(64, 64);
        buffer.Ensure(64, 64);
        Assert.Equal(1, buffer.Reallocations);
        buffer.Ensure(128, 64);
        Assert.Equal(2, buffer.Reallocations);
        Assert.Equal(128 * 64 * 4, buffer.Bytes.Length);
        Assert.Throws<ArgumentOutOfRangeException>(() => buffer.Ensure(0, 8));
    }

    [Fact]
    public void Render_into_a_pinned_array_matches_the_span_path()
    {
        using var world = TwoByTwoWithSpecies();
        var viaSpan = new FrameBuffer();
        world.RenderFrame(viaSpan, Palette.Default);

        var viaPointer = new byte[viaSpan.Bytes.Length];
        var pin = GCHandle.Alloc(viaPointer, GCHandleType.Pinned);
        try
        {
            world.RenderInto(pin.AddrOfPinnedObject(), viaSpan.RowPitch, Palette.Default);
        }
        finally
        {
            pin.Free();
        }

        Assert.Equal(viaSpan.Bytes, viaPointer);
    }

    [Fact]
    public void Palette_rejects_the_wrong_number_of_slots()
    {
        Assert.Throws<ArgumentException>(() => new Palette(new uint[3]));
        Assert.Equal(16, Palette.Slots);
    }

    [Fact]
    public void Render_reports_a_too_small_pitch_instead_of_overrunning()
    {
        using var world = TwoByTwoWithSpecies();
        var buffer = new byte[4];
        var pin = GCHandle.Alloc(buffer, GCHandleType.Pinned);
        try
        {
            var ex = Assert.Throws<EcosimException>(
                () => world.RenderInto(pin.AddrOfPinnedObject(), 4, Palette.Default));
            Assert.Equal(AbiConstants.ErrInvalid, ex.Code);
        }
        finally
        {
            pin.Free();
        }
    }
}
