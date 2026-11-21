using Ecosim.Core.Interop;

namespace Ecosim.Core;

/// <summary>
/// The facade the application talks to. It keeps no simulation state of its
/// own: state lives in native memory, and this type only translates between
/// managed idioms and the ABI.
/// </summary>
public sealed class World : IDisposable
{
    private readonly NativeWorld _native;

    public World(WorldConfig cfg)
    {
        _native = NativeWorld.Create(cfg);
        Config = cfg;
    }

    public WorldConfig Config { get; }

    public int Width => Config.Width;

    public int Height => Config.Height;

    public long Generation => _native.Generation;

    public GridTopology Topology
    {
        get => _native.Topology;
        set => _native.Topology = value;
    }

    public void Step(int nSteps) => _native.Step(nSteps);

    /// <summary>
    /// Applies a whole brush stroke in one call across the border.
    ///
    /// Ops are clipped to the grid first: dragging fast routinely sends the
    /// cursor off the edge, and "paint what is on the board, ignore the rest"
    /// is the behaviour a person expects from a brush. The engine still refuses
    /// genuinely bogus rectangles (inverted, unknown species), which is why the
    /// clipping happens here rather than in the ABI.
    /// </summary>
    public void Paint(IReadOnlyList<PaintOp> ops)
    {
        ArgumentNullException.ThrowIfNull(ops);
        if (ops.Count == 0) return;

        var clipped = ClipToGrid(ops);
        if (clipped.Count > 0) _native.Paint(clipped);
    }

    private List<PaintOp>? _clipBuffer = new();

    private IReadOnlyList<PaintOp> ClipToGrid(IReadOnlyList<PaintOp> ops)
    {
        var buffer = _clipBuffer ??= new List<PaintOp>(ops.Count);
        buffer.Clear();
        foreach (var op in ops)
        {
            var x0 = Math.Max(op.X0, 0);
            var y0 = Math.Max(op.Y0, 0);
            var x1 = Math.Min(op.X1, Width);
            var y1 = Math.Min(op.Y1, Height);
            if (x0 < x1 && y0 < y1) buffer.Add(new PaintOp(x0, y0, x1, y1, op.Species));
        }
        return buffer;
    }

    public SimStats Stats() => _native.Stats();

    public void Clear(int fillSpecies = 0) => _native.Clear(fillSpecies);

    /// <summary>Replaces the species plane wholesale. Length must match the
    /// grid; living cells get their species' initial energy.</summary>
    public void LoadSpecies(byte[] species) => _native.LoadSpecies(species);

    public void Randomize(in SeedConfig config) => _native.Randomize(config);

    /// <summary>FNV-1a over the species plane; the tests use it to prove two
    /// runs agree cell for cell.</summary>
    public ulong Hash() => _native.Hash();

    /// <summary>Renders into the buffer, resizing it first if the world changed.</summary>
    public void RenderFrame(FrameBuffer buffer, in Palette palette)
    {
        buffer.Ensure(Width, Height);
        _native.RenderFrame(buffer.AsMemory(), buffer.RowPitch, palette);
    }

    public SpeciesSnapshot Capture(bool includeHeat = false) => _native.Capture(includeHeat);

    /// <summary>
    /// Captures exactly the planes the palette will ask for. The render path and
    /// the capture path used to agree by convention, and getting that wrong
    /// surfaces as a confusing "engine failed to start" dialog; this is the one
    /// call that cannot be mismatched.
    /// </summary>
    public SpeciesSnapshot CaptureFor(in Palette palette) => Capture(palette.NeedsHeatPlanes);

    /// <summary>Paints an immutable snapshot; no engine lock is taken.</summary>
    public static void RenderSnapshotInto(SpeciesSnapshot snapshot, IntPtr bgra, int rowPitch,
                                          in Palette palette)
        => NativeWorld.RenderSnapshot(snapshot, bgra, rowPitch, palette);

    /// <summary>Everything the UI needs to draw a slider per tunable.</summary>
    public static IReadOnlyList<ParamDescription> ParamDescriptions =>
        NativeWorld.DescribeParameters();

    public int GetParam(int index) => _native.GetParam(index);

    /// <summary>Hot: takes effect from the next step. Out-of-range values are
    /// refused by the engine, which names the allowed range in the message.</summary>
    public void SetParam(int index, int value) => _native.SetParam(index, value);


    /// <summary>Zero-copy variant for a locked bitmap's back buffer.</summary>
    public void RenderInto(IntPtr bgra, int rowPitch, in Palette palette)
        => _native.RenderToPointer(bgra, rowPitch, palette);

    public string ToRle() => _native.ToRle();

    /// <summary>Places the pattern with its top-left corner at (0, 0).</summary>
    public void FromRle(string text) => FromRleAt(text, 0, 0);

    public void FromRleAt(string text, int originX, int originY)
        => _native.FromRleAt(text, originX, originY);

    /// <summary>The handle-level object, for the few callers that must hold the
    /// ABI lock across two operations (rendering, snapshotting).</summary>
    internal NativeWorld Native => _native;

    public void Dispose() => _native.Dispose();
}
