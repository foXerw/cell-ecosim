namespace Ecosim.Core;

public sealed record ParamGroup(string Section, IReadOnlyList<ParamSlider> Sliders);

/// <summary>
/// The parameter panel's contents, generated from the engine's description
/// table. Adding a parameter in C++ makes a slider appear here - there is no
/// list on the C# side to keep in sync, which is the point of the design
/// (see docs/architecture.md).
/// </summary>
public sealed class ParamsModel
{
    private readonly List<ParamSlider> _sliders;

    public ParamsModel(World world)
    {
        ArgumentNullException.ThrowIfNull(world);
        _sliders = [.. World.ParamDescriptions.Select(d => new ParamSlider(world, d))];
        Groups = [.. _sliders.GroupBy(s => s.Section)
                              .Select(g => new ParamGroup(g.Key, g.ToList()))];
    }

    public IReadOnlyList<ParamSlider> Sliders => _sliders;

    public IReadOnlyList<ParamGroup> Groups { get; }

    public ParamSlider? Find(string name)
        => _sliders.FirstOrDefault(s => s.Description.Name == name);

    public void Refresh()
    {
        foreach (var slider in _sliders) slider.Refresh();
    }
}
