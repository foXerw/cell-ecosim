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

    /// <summary>The handle-level object, for the few callers that must hold the
    /// ABI lock across two operations (rendering, snapshotting).</summary>
    internal NativeWorld Native => _native;

    public void Dispose() => _native.Dispose();
}
