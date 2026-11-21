using System.Runtime.InteropServices;

namespace Ecosim.Core.Abi;

/// <summary>Mirror of EcosimHistorySample (80 bytes, pack 4).</summary>
[StructLayout(LayoutKind.Sequential, Pack = 4)]
internal struct EcosimHistorySampleDto
{
    public int struct_size;
    public ulong generation;
    public int alive_count;

    [MarshalAs(UnmanagedType.ByValArray, SizeConst = AbiConstants.MaxSpecies)]
    public int[] species_count;

    public static EcosimHistorySampleDto Create() => new()
    {
        struct_size = Marshal.SizeOf<EcosimHistorySampleDto>(),
        species_count = new int[AbiConstants.MaxSpecies],
    };
}
