using System.Runtime.InteropServices;

namespace Ecosim.Core.Abi;

/// <summary>Mirror of EcosimGridView. Every pointer is BORROWED: valid only
/// until the next mutating call, and only while the world's lock is held.</summary>
[StructLayout(LayoutKind.Sequential, Pack = 4)]
internal struct EcosimGridViewDto
{
    public int struct_size;
    public int w;
    public int h;
    public int row_pitch_cells;
    public IntPtr species;
    public IntPtr resource;
    public IntPtr energy;
    public IntPtr age;

    public static EcosimGridViewDto Create() => new()
    {
        struct_size = Marshal.SizeOf<EcosimGridViewDto>(),
    };
}
