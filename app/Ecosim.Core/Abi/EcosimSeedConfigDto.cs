using System.Runtime.InteropServices;

namespace Ecosim.Core.Abi;

/// <summary>Mirror of EcosimSeedConfig.</summary>
[StructLayout(LayoutKind.Sequential, Pack = 4)]
internal struct EcosimSeedConfigDto
{
    public int struct_size;
    public int species;
    public uint density_permille;
    public ulong seed;

    public static EcosimSeedConfigDto Create(SeedConfig cfg) => new()
    {
        struct_size = Marshal.SizeOf<EcosimSeedConfigDto>(),
        species = cfg.Species,
        density_permille = (uint)cfg.DensityPermille,
        seed = cfg.Seed,
    };
}
