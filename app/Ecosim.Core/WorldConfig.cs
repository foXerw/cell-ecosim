namespace Ecosim.Core;

public enum RuleMode
{
    /// <summary>Conway-style: birth/survive masks over the 8-neighbour count.</summary>
    BirthSurvive = 0,

    /// <summary>Multi-species pipeline. Rejected by the engine until milestone M2.</summary>
    Ecosystem = 1,
}

public enum GridTopology
{
    Torus = 0,
    Bounded = 1,
}

/// <summary>Managed-side description of a world. Converted to the fixed-layout
/// ABI struct only at the moment of the call, so callers never think in
/// marshalling terms.</summary>
public record struct WorldConfig
{
    public int Width { get; set; }

    public int Height { get; set; }

    public RuleMode Mode { get; set; }

    public GridTopology Topology { get; set; }

    /// <summary>0 lets the engine choose; -1 is rejected by the ABI.</summary>
    public int Threads { get; set; }

    /// <summary>Index = live neighbour count. Shorter arrays are zero-padded.</summary>
    public byte[] BirthMask { get; set; }

    public byte[] SurviveMask { get; set; }

    public ulong Seed { get; set; }

    /// <summary>B3/S23 on a torus: the classic Game of Life.</summary>
    public static WorldConfig Default(int width, int height) => new()
    {
        Width = width,
        Height = height,
        Mode = RuleMode.BirthSurvive,
        Topology = GridTopology.Torus,
        Threads = 0,
        BirthMask = new byte[] { 0, 0, 0, 1, 0, 0, 0, 0, 0 },
        SurviveMask = new byte[] { 0, 0, 1, 1, 0, 0, 0, 0, 0 },
        Seed = 0,   // 0 = time-based; set ECOSIM_SEED for a reproducible opening
    };

    /// <summary>HighLife: B36/S23, adds replicators.</summary>
    public static WorldConfig HighLife(int width, int height) => new()
    {
        Width = width,
        Height = height,
        Mode = RuleMode.BirthSurvive,
        Topology = GridTopology.Torus,
        Threads = 0,
        BirthMask = new byte[] { 0, 0, 0, 1, 0, 0, 1, 0, 0 },
        SurviveMask = new byte[] { 0, 0, 1, 1, 0, 0, 0, 0, 0 },
        Seed = 0,
    };
}

/// <summary>Arguments for <see cref="World.Randomize"/>.</summary>
public record struct SeedConfig
{
    public int Species { get; set; }

    /// <summary>Probability a cell starts alive, in per mille (0..1000).</summary>
    public int DensityPermille { get; set; }

    /// <summary>0 means "use the world's configured seed".</summary>
    public ulong Seed { get; set; }
}

/// <summary>One half-open rectangle to write. The engine takes an array of
/// these so a whole brush stroke costs a single call across the border.</summary>
public readonly record struct PaintOp(int X0, int Y0, int X1, int Y1, int Species);

/// <summary>A snapshot of the population. Immutable on purpose: the UI binds
/// to it and must never see it change under a binding.</summary>
public sealed record SimStats(long Generation, int Alive, int Width, int Height,
                              int[] SpeciesCounts)
{
    public int CountOf(int species)
        => species >= 0 && species < SpeciesCounts.Length ? SpeciesCounts[species] : 0;
}
