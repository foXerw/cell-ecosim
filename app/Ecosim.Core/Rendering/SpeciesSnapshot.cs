namespace Ecosim.Core;

/// <summary>
/// An immutable copy of the species plane plus the numbers that go with it.
///
/// The clock thread captures one right after each step; the render pump paints
/// from it without ever taking the engine lock, so a long simulation batch can
/// no longer stall a repaint (and a repaint can no longer stall a step).
/// </summary>
public sealed class SpeciesSnapshot
{
    internal SpeciesSnapshot(byte[] species, int width, int height, long generation, int alive,
                             byte[]? energy, byte[]? age, byte[]? resource)
    {
        Species = species;
        Width = width;
        Height = height;
        Generation = generation;
        Alive = alive;
        Energy = energy;
        Age = age;
        Resource = resource;
    }

    public byte[] Species { get; }

    public int Width { get; }

    public int Height { get; }

    public long Generation { get; }

    public int Alive { get; }

    /// <summary>Present only when the snapshot was captured for a heat layer.
    /// Raw little-endian planes (u16 per cell for energy and age), handed
    /// straight to the renderer without a per-frame reinterpretation.</summary>
    public byte[]? Energy { get; }

    public byte[]? Age { get; }

    public byte[]? Resource { get; }

    public bool HasHeatPlanes => Energy is not null && Age is not null && Resource is not null;
}
