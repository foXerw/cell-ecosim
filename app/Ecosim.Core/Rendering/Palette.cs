using Ecosim.Core.Abi;

namespace Ecosim.Core;

/// <summary>What the renderer colours cells by (mirrors EcosimRenderLayer).</summary>
public enum RenderLayer
{
    Species = 0,
    Energy = 1,
    Resource = 2,
    Age = 3,
}

/// <summary>One BGRA colour per species slot. Index 0 is empty ground.</summary>
public readonly struct Palette : IEquatable<Palette>
{
    public const int Slots = AbiConstants.MaxSpecies;

    private readonly uint[] _colors;

    /// <summary>Heat layers need the extra planes in a snapshot; the species
    /// layer does not, so capturing them is opt-in.</summary>
    public RenderLayer Layer { get; }

    public Palette(uint[] colors, RenderLayer layer = RenderLayer.Species)
    {
        if (colors is null || colors.Length != Slots)
        {
            throw new ArgumentException($"调色盘需要 {Slots} 个 BGRA 颜色", nameof(colors));
        }
        _colors = (uint[])colors.Clone();
        Layer = layer;
    }

    public uint this[int species] => _colors[species % Slots];

    /// <summary>Deep background plus the milestone-M1 species colours:
    /// species 1 doubles as the Life "alive" colour (appendix A.7).</summary>
    public static Palette Default { get; } = new(new uint[Slots]
    {
        0xFF14181D,  // 0  empty
        0xFF6FD36F,  // 1  alga / Life alive (green)
        0xFF40C8E8,  // 2  grazer (amber)
        0xFF5860F0,  // 3  hunter (red)
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    });

    /// <summary>Same colours, different question: "show me energy" and so on.</summary>
    public Palette WithLayer(RenderLayer layer)
        => layer == Layer ? this : new Palette(_colors, layer);

    public bool NeedsHeatPlanes => Layer != RenderLayer.Species;

    internal EcosimPaletteDto ToDto() => EcosimPaletteDto.Create((uint[])_colors.Clone(), (int)Layer);

    // The layer is part of equality: "these colours, shown as energy" is not
    // the same instruction as "these colours, shown as species".
    public bool Equals(Palette other)
        => Layer == other.Layer && _colors.AsSpan().SequenceEqual(other._colors);

    public override bool Equals(object? obj) => obj is Palette other && Equals(other);

    public override int GetHashCode()
    {
        var hash = new HashCode();
        hash.Add(Layer);
        foreach (var c in _colors) hash.Add(c);
        return hash.ToHashCode();
    }
}
