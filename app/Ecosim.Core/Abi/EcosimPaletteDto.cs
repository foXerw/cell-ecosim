using System.Runtime.InteropServices;

namespace Ecosim.Core.Abi;

/// <summary>Mirror of EcosimPalette: one BGRA colour per species slot.</summary>
[StructLayout(LayoutKind.Sequential, Pack = 4)]
internal struct EcosimPaletteDto
{
    public int struct_size;

    [MarshalAs(UnmanagedType.ByValArray, SizeConst = AbiConstants.MaxSpecies)]
    public uint[] cell_bgra;

    /// <summary>Appended in milestone M3: 0 species, 1 energy, 2 resource, 3 age.</summary>
    public int layer;

    public static EcosimPaletteDto Create(uint[] colors, int layer) => new()
    {
        struct_size = Marshal.SizeOf<EcosimPaletteDto>(),
        cell_bgra = colors is { Length: AbiConstants.MaxSpecies }
            ? colors
            : throw new ArgumentException($"调色盘需要 {AbiConstants.MaxSpecies} 个 BGRA 颜色",
                                         nameof(colors)),
        layer = layer,
    };
}
