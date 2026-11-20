using System.Runtime.InteropServices;

namespace Ecosim.Core.Abi;

/// <summary>Mirror of EcosimPaintOp. The rectangle is half-open: [x0, x1) × [y0, y1).</summary>
[StructLayout(LayoutKind.Sequential, Pack = 4)]
internal struct EcosimPaintOpDto
{
    public int struct_size;
    public int x0;
    public int y0;
    public int x1;
    public int y1;
    public int species;

    public static EcosimPaintOpDto From(PaintOp op) => new()
    {
        struct_size = Marshal.SizeOf<EcosimPaintOpDto>(),
        x0 = op.X0,
        y0 = op.Y0,
        x1 = op.X1,
        y1 = op.Y1,
        species = op.Species,
    };
}
