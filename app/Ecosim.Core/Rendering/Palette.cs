using Ecosim.Core.Abi;

namespace Ecosim.Core;

/// <summary>One BGRA colour per species slot. Index 0 is empty ground.</summary>
public readonly struct Palette : IEquatable<Palette>
{
    public const int Slots = AbiConstants.MaxSpecies;

    private readonly uint[] _colors;

    public Palette(uint[] colors)
    {
        if (colors is null || colors.Length != Slots)
        {
            throw new ArgumentException($"调色盘需要 {Slots} 个 BGRA 颜色", nameof(colors));
        }
        _colors = (uint[])colors.Clone();
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

    internal EcosimPaletteDto ToDto() => EcosimPaletteDto.Create((uint[])_colors.Clone());

    public bool Equals(Palette other) => _colors.AsSpan().SequenceEqual(other._colors);

    public override bool Equals(object? obj) => obj is Palette other && Equals(other);

    public override int GetHashCode()
    {
        var hash = new HashCode();
        foreach (var c in _colors) hash.Add(c);
        return hash.ToHashCode();
    }
}
