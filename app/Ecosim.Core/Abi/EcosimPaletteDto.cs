using System.Runtime.InteropServices;

namespace Ecosim.Core.Abi;

/// <summary>Mirror of EcosimPalette: one BGRA colour per species slot.</summary>
[StructLayout(LayoutKind.Sequential, Pack = 4)]
internal struct EcosimPaletteDto
{
    public int struct_size;

    [MarshalAs(UnmanagedType.ByValArray, SizeConst = AbiConstants.MaxSpecies)]
    public uint[] cell_bgra;

    public static EcosimPaletteDto Create(uint[] colors) => new()
    {
        struct_size = Marshal.SizeOf<EcosimPaletteDto>(),
        cell_bgra = colors is { Length: AbiConstants.MaxSpecies }
            ? colors
            : throw new ArgumentException($"调色盘需要 {AbiConstants.MaxSpecies} 个 BGRA 颜色",
                                         nameof(colors)),
    };
}
