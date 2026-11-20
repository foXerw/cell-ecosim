using System.Runtime.InteropServices;

namespace Ecosim.Core.Abi;

/// <summary>Mirror of EcosimStats.</summary>
[StructLayout(LayoutKind.Sequential, Pack = 4)]
internal struct EcosimStatsDto
{
    public int struct_size;
    public ulong generation;
    public int alive_count;
    public int w;
    public int h;

    [MarshalAs(UnmanagedType.ByValArray, SizeConst = AbiConstants.MaxSpecies)]
    public int[] species_count;

    public static EcosimStatsDto Create() => new()
    {
        struct_size = Marshal.SizeOf<EcosimStatsDto>(),
        species_count = new int[AbiConstants.MaxSpecies],
    };
}
