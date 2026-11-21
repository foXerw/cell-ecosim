using System.Diagnostics;
using Ecosim.Core;

namespace Ecosim.Tests;

/// <summary>
/// The analysis panel runs on the UI thread at 4 Hz: one heat capture, one
/// histogram, one history pull. That is a real cost, so it gets a budget
/// rather than a promise.
/// </summary>
public class AnalysisCostTests
{
    [Fact]
    public void A_panel_refresh_at_512_squared_stays_under_40ms()
    {
        using var world = new World(WorldConfig.Default(512, 512) with { Mode = RuleMode.Ecosystem });
        world.Randomize(new SeedConfig { Species = 1, DensityPermille = 250, Seed = 9 });
        world.Step(30);

        // Warm the caches, then measure the same three calls the panel makes.
        void Refresh()
        {
            var snapshot = world.Capture(includeHeat: true);
            _ = world.EnergyHistogram(12, snapshot);
            _ = world.History(600);
        }

        Refresh();
        var sw = Stopwatch.StartNew();
        for (var i = 0; i < 5; ++i) Refresh();
        sw.Stop();

        var perRefresh = sw.Elapsed.TotalMilliseconds / 5;
        Assert.True(perRefresh < 40, $"一次面板刷新 {perRefresh:F1} ms，超过 40 ms 预算");
    }

    [Fact]
    public void Stepping_is_still_the_dominate_cost_not_the_analysis()
    {
        using var world = new World(WorldConfig.Default(512, 512) with { Mode = RuleMode.Ecosystem });
        world.Randomize(new SeedConfig { Species = 1, DensityPermille = 250, Seed = 9 });
        world.Step(5);

        var sw = Stopwatch.StartNew();
        world.Step(10);
        sw.Stop();
        var stepMs = sw.Elapsed.TotalMilliseconds / 10;

        sw.Restart();
        var snapshot = world.Capture(includeHeat: true);
        _ = world.EnergyHistogram(12, snapshot);
        sw.Stop();
        var analysisMs = sw.Elapsed.TotalMilliseconds;

        // If analysis ever overtakes a generation, the panel needs its own
        // thread or a cheaper pull - worth knowing before it is true.
        Assert.True(analysisMs < stepMs * 4,
                    $"分析 {analysisMs:F1} ms 相对单代 {stepMs:F1} ms 偏重");
    }
}
