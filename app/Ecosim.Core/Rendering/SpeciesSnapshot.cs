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
    internal SpeciesSnapshot(byte[] species, int width, int height, long generation, int alive)
    {
        Species = species;
        Width = width;
        Height = height;
        Generation = generation;
        Alive = alive;
    }

    public byte[] Species { get; }

    public int Width { get; }

    public int Height { get; }

    public long Generation { get; }

    public int Alive { get; }
}
