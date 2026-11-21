namespace Ecosim.Core;

/// <summary>
/// One row of the engine's parameter table. The UI builds a slider per entry,
/// so a new tunable appears in the interface without any C# change (design
/// §11.2) - which is also why the label comes from native rather than a
/// resource file that could drift out of sync.
/// </summary>
public sealed record ParamDescription(
    int Index,
    int Group,
    int Species,
    int Kind,
    int Min,
    int Max,
    int Default,
    string Name,
    string Label)
{
    /// <summary>Permille parameters run 0..1000, species scales 0..10000.</summary>
    public bool IsPermille => Kind == 0;

    public bool IsPerSpecies => Group == 1;

    /// <summary>A step that makes the slider usable without losing precision.</summary>
    public int Step => IsPermille ? 10 : 50;

    /// <summary>Where the slider belongs in the panel.</summary>
    public string Section => IsPerSpecies ? $"物种 {Species}" : "全局";
}
