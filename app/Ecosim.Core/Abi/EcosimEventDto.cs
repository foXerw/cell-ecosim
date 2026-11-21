using System.Runtime.InteropServices;

namespace Ecosim.Core.Abi;

/// <summary>Mirror of EcosimEvent (24 bytes, pack 4).</summary>
[StructLayout(LayoutKind.Sequential, Pack = 4)]
internal struct EcosimEventDto
{
    public int struct_size;
    public ulong generation;
    public int kind;
    public int species;
    public int count;

    public static EcosimEventDto[] CreateBuffer(int capacity)
    {
        var buffer = new EcosimEventDto[capacity];
        for (var i = 0; i < capacity; ++i)
        {
            buffer[i].struct_size = Marshal.SizeOf<EcosimEventDto>();
        }
        return buffer;
    }
}
