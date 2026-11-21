namespace Ecosim.Core;

/// <summary>One generation's census, as kept by the engine's history ring.</summary>
public sealed record HistorySample(long Generation, int Alive, int[] SpeciesCounts)
{
    public int CountOf(int species)
        => species >= 0 && species < SpeciesCounts.Length ? SpeciesCounts[species] : 0;
}
