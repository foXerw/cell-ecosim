using System.Runtime.InteropServices;
using Ecosim.Core;

namespace Ecosim.Tests;

public class LayerTests
{
    private static World Ecosystem(int size = 32, ulong seed = 3)
    {
        var world = new World(WorldConfig.Default(size, size) with { Mode = RuleMode.Ecosystem });
        world.Randomize(new SeedConfig { Species = 1, DensityPermille = 350, Seed = seed });
        world.Step(5);
        return world;
    }

    private static byte[] RenderSnapshot(SpeciesSnapshot snapshot, in Palette palette)
    {
        var buffer = new FrameBuffer();
        buffer.Ensure(snapshot.Width, snapshot.Height);
        var pin = GCHandle.Alloc(buffer.Bytes, GCHandleType.Pinned);
        try
        {
            World.RenderSnapshotInto(snapshot, pin.AddrOfPinnedObject(), buffer.RowPitch, palette);
        }
        finally
        {
            pin.Free();
        }
        return buffer.Bytes;
    }

    [Fact]
    public void A_species_layer_snapshot_carries_one_plane_only()
    {
        using var world = Ecosystem();
        var plain = world.Capture();
        Assert.False(plain.HasHeatPlanes);

        var heat = world.Capture(includeHeat: true);
        Assert.True(heat.HasHeatPlanes);
        Assert.Equal(heat.Species, plain.Species);
        Assert.Equal(32 * 32, heat.Energy!.Length / 2);
        Assert.Equal(32 * 32, heat.Resource!.Length);
    }

    [Fact]
    public void Heat_layers_produce_a_visible_range_of_colours()
    {
        using var world = Ecosystem(size: 64, seed: 3);
        var baseline = Measure(Palette.Default, world);
        var energy = Measure(Palette.Default.WithLayer(RenderLayer.Energy), world);
        var age = Measure(Palette.Default.WithLayer(RenderLayer.Age), world);

        // What a heat map owes the reader: it must not be flat black, and it
        // must show differences between cells. How many distinct colours one
        // board produces is a property of the population, not of the renderer,
        // so the assertion is about spread, not about a count.
        Assert.True(energy.meanLuma > 60, $"能量层平均亮度只有 {energy.meanLuma:F1}");
        Assert.True(energy.max > energy.min, "能量层所有格子同色，看不出差别");
        Assert.True(age.max > age.min, "年龄层所有格子同色");
        Assert.NotEqual(baseline.colours, energy.colours);
    }

    private static (double meanLuma, int min, int max, HashSet<uint> colours) Measure(
        in Palette palette, World world)
    {
        var bytes = RenderSnapshot(world.CaptureFor(palette), palette);
        double sum = 0;
        var seen = new HashSet<uint>();
        var min = int.MaxValue;
        var max = int.MinValue;
        for (var i = 0; i < bytes.Length; i += 4)
        {
            var bgra = BitConverter.ToUInt32(bytes, i);
            seen.Add(bgra);
            var luma = (int)(((bgra & 0xFF) * 114 + ((bgra >> 8) & 0xFF) * 587 +
                              ((bgra >> 16) & 0xFF) * 299) / 1000);
            sum += luma;
            min = Math.Min(min, luma);
            max = Math.Max(max, luma);
        }
        return (sum / (bytes.Length / 4), min, max, seen);
    }

    [Fact]
    public void CaptureFor_never_mismatches_the_palette()
    {
        // The bug this pins: a heat palette fed by a species-only snapshot used
        // to fail at startup with an "engine failed to start" dialog.
        using var world = Ecosystem();
        Assert.False(world.CaptureFor(Palette.Default).HasHeatPlanes);
        Assert.True(world.CaptureFor(Palette.Default.WithLayer(RenderLayer.Energy)).HasHeatPlanes);

        // And each of those renders without complaint.
        foreach (var layer in new[]
                 { RenderLayer.Species, RenderLayer.Energy, RenderLayer.Resource, RenderLayer.Age })
        {
            var palette = Palette.Default.WithLayer(layer);
            _ = RenderSnapshot(world.CaptureFor(palette), palette);
        }
    }

    [Fact]
    public void Heat_layer_snapshot_renders_identically_to_the_live_world()
    {
        using var world = Ecosystem();
        var palette = Palette.Default.WithLayer(RenderLayer.Energy);

        var live = new FrameBuffer();
        world.RenderFrame(live, palette);

        var viaSnapshot = RenderSnapshot(world.Capture(includeHeat: true), palette);
        Assert.Equal(live.Bytes, viaSnapshot);
    }

    [Fact]
    public void Species_layer_snapshot_renders_identically_to_the_live_world()
    {
        using var world = Ecosystem();
        var palette = Palette.Default;

        var live = new FrameBuffer();
        world.RenderFrame(live, palette);

        var viaSnapshot = RenderSnapshot(world.Capture(), palette);
        Assert.Equal(live.Bytes, viaSnapshot);
    }

    [Fact]
    public void Asking_for_heat_from_a_thin_snapshot_says_what_is_missing()
    {
        using var world = Ecosystem();
        var thin = world.Capture();
        var palette = Palette.Default.WithLayer(RenderLayer.Resource);

        var ex = Assert.Throws<EcosimException>(
            () => RenderSnapshot(thin, palette));
        Assert.Contains("能量/年龄/resource", ex.Message);
    }

    [Fact]
    public void Every_layer_is_distinct_and_the_species_layer_is_the_default()
    {
        using var world = Ecosystem();
        Assert.Equal(RenderLayer.Species, Palette.Default.Layer);
        Assert.False(Palette.Default.NeedsHeatPlanes);

        var buffers = new Dictionary<RenderLayer, byte[]>();
        foreach (var layer in new[]
                 { RenderLayer.Species, RenderLayer.Energy, RenderLayer.Resource, RenderLayer.Age })
        {
            var palette = Palette.Default.WithLayer(layer);
            var snapshot = world.Capture(includeHeat: palette.NeedsHeatPlanes);
            buffers[layer] = RenderSnapshot(snapshot, palette);
            Assert.True(layer == RenderLayer.Species || palette.NeedsHeatPlanes);
        }

        Assert.False(buffers[RenderLayer.Species].AsSpan().SequenceEqual(buffers[RenderLayer.Energy]));
        Assert.False(buffers[RenderLayer.Energy].AsSpan().SequenceEqual(buffers[RenderLayer.Resource]));
        Assert.False(buffers[RenderLayer.Age].AsSpan().SequenceEqual(buffers[RenderLayer.Resource]));
    }

    [Fact]
    public void WithLayer_is_idempotent_for_the_same_layer()
    {
        var palette = Palette.Default.WithLayer(RenderLayer.Age);
        // Palette is a struct, so compare by equality rather than identity.
        Assert.Equal(palette, palette.WithLayer(RenderLayer.Age));
        Assert.NotEqual(palette, palette.WithLayer(RenderLayer.Species));
        Assert.Equal(RenderLayer.Species, palette.WithLayer(RenderLayer.Species).Layer);
    }

    [Fact]
    public void The_clock_captures_heat_only_when_the_layer_needs_it()
    {
        using var world = Ecosystem(size: 16, seed: 9);
        using var clock = new SimClock(world, 200);
        Assert.False(clock.CaptureHeat);
        clock.CaptureHeat = true;
        clock.Start();
        var deadline = DateTime.UtcNow.AddSeconds(5);
        while (clock.LatestSnapshot is null && DateTime.UtcNow < deadline) Task.Delay(10).Wait();
        clock.Stop();
        Assert.NotNull(clock.LatestSnapshot);
        Assert.True(clock.LatestSnapshot!.HasHeatPlanes);
    }
}
