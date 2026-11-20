using System.Runtime.InteropServices;

namespace Ecosim.Core.Abi;

/// <summary>Byte-for-byte mirror of EcosimConfig. Field names stay lower_case
/// on purpose so they can be diffed against the C header at a glance.</summary>
[StructLayout(LayoutKind.Sequential, Pack = 4)]
internal struct EcosimConfigDto
{
    public int struct_size;
    public int w;
    public int h;
    public int mode;
    public int topology;
    public int threads;

    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 9)]
    public byte[] birth_mask;

    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 9)]
    public byte[] survive_mask;

    public ulong rng_seed;

    public static EcosimConfigDto Create(WorldConfig cfg) => new()
    {
        struct_size = Marshal.SizeOf<EcosimConfigDto>(),
        w = cfg.Width,
        h = cfg.Height,
        mode = (int)cfg.Mode,
        topology = (int)cfg.Topology,
        threads = cfg.Threads,
        birth_mask = Pad(cfg.BirthMask),
        survive_mask = Pad(cfg.SurviveMask),
        rng_seed = cfg.Seed,
    };

    private static byte[] Pad(byte[]? values)
    {
        var padded = new byte[9];
        if (values is null) return padded;
        Array.Copy(values, padded, Math.Min(values.Length, 9));
        return padded;
    }
}
