using System.Runtime.InteropServices;

namespace Ecosim.Core.Abi;

/// <summary>Mirror of EcosimParams (140 bytes, pack 4).</summary>
[StructLayout(LayoutKind.Sequential, Pack = 4)]
internal struct EcosimParamsDto
{
    public int struct_size;
    public int light_permille;
    public int fertility_permille;

    [MarshalAs(UnmanagedType.ByValArray, SizeConst = AbiConstants.MaxSpecies)]
    public int[] metabolism_scale_permille;

    [MarshalAs(UnmanagedType.ByValArray, SizeConst = AbiConstants.MaxSpecies)]
    public int[] repro_scale_permille;

    public static EcosimParamsDto Create() => new()
    {
        struct_size = Marshal.SizeOf<EcosimParamsDto>(),
        light_permille = 350,
        fertility_permille = 200,
        metabolism_scale_permille = Filled(AbiConstants.MaxSpecies, 1000),
        repro_scale_permille = Filled(AbiConstants.MaxSpecies, 1000),
    };

    private static int[] Filled(int count, int value)
    {
        var array = new int[count];
        for (var i = 0; i < count; ++i) array[i] = value;
        return array;
    }
}

/// <summary>Mirror of EcosimParamDesc (48 bytes, pack 4). name/label are static
/// UTF-8 owned by the engine: copy them out immediately, never keep the pointer.</summary>
[StructLayout(LayoutKind.Sequential, Pack = 4)]
internal struct EcosimParamDescDto
{
    public int struct_size;
    public int index;
    public int group;
    public int species;
    public int kind;
    public int min_value;
    public int max_value;
    public int default_value;
    public IntPtr name;
    public IntPtr label;

    public static EcosimParamDescDto Create() => new()
    {
        struct_size = Marshal.SizeOf<EcosimParamDescDto>(),
    };
}
