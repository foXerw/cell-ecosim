namespace Ecosim.Core;

public enum SimEventKind
{
    Extinction = 0,
    Outbreak = 1,
    Appearance = 2,
}

/// <summary>
/// Something the engine noticed about its own populations. Pulled rather than
/// pushed: the engine never calls into managed code (design appendix A.1).
/// </summary>
public sealed record SimEvent(long Generation, SimEventKind Kind, int Species, int Count)
{
    public string Describe(string? speciesName)
    {
        var who = speciesName ?? $"物种 {Species}";
        return Kind switch
        {
            SimEventKind.Extinction => $"第 {Generation:N0} 代｜{who} 灭绝",
            SimEventKind.Outbreak => $"第 {Generation:N0} 代｜{who} 激增（{Count:N0} 个）",
            SimEventKind.Appearance => $"第 {Generation:N0} 代｜{who} 重新出现（{Count:N0} 个）",
            _ => $"第 {Generation:N0} 代｜{who} 事件 {Kind}",
        };
    }
}
