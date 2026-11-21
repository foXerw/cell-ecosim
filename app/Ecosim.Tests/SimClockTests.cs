using Ecosim.Core;

namespace Ecosim.Tests;

public class SimClockTests
{
    [Fact]
    public async Task Clock_advances_generation_and_stops_cleanly()
    {
        using var world = new World(WorldConfig.Default(16, 16));
        using var clock = new SimClock(world, targetGenPerSec: 200);

        var reached = new TaskCompletionSource<long>(TaskCreationOptions.RunContinuationsAsynchronously);
        clock.Tick += (_, e) =>
        {
            if (e.Generation >= 20) reached.TrySetResult(e.Generation);
        };

        clock.Start();
        var won = await Task.WhenAny(reached.Task, Task.Delay(TimeSpan.FromSeconds(10))) == reached.Task;
        Assert.True(won, "generation 未推进");

        await clock.StopAsync();
        Assert.False(clock.IsRunning);

        var settled = world.Generation;
        await Task.Delay(200);
        Assert.Equal(settled, world.Generation);   // stopped means stopped
    }

    [Fact]
    public void Starting_twice_is_harmless_and_reports_running()
    {
        using var world = new World(WorldConfig.Default(8, 8));
        using var clock = new SimClock(world, 50);
        clock.Start();
        clock.Start();
        Assert.True(clock.IsRunning);
        clock.Stop();
        Assert.False(clock.IsRunning);
    }

    [Fact]
    public void Speed_below_one_is_rejected()
    {
        using var world = new World(WorldConfig.Default(8, 8));
        using var clock = new SimClock(world, 10);
        Assert.Throws<ArgumentOutOfRangeException>(() => clock.SetSpeed(0));
        Assert.Throws<ArgumentOutOfRangeException>(() =>
        {
            var ignored = new SimClock(world, -1);
            ignored.Dispose();
        });
    }

    [Fact]
    public async Task Tick_carries_generation_and_measured_step_cost()
    {
        using var world = new World(WorldConfig.Default(64, 64));
        using var clock = new SimClock(world, 500);
        TickEventArgs? last = null;
        clock.Tick += (_, e) => last = e;
        clock.Start();
        await Task.Delay(300);
        await clock.StopAsync();

        Assert.NotNull(last);
        Assert.True(last!.Generation > 0);
        Assert.True(last.StepMs >= 0.0, "步进耗时应为非负数");
    }

    [Theory]
    // A 46.9 ms tick at "30 gen/s" is 1.4 generations. Rounding throws the 0.4
    // away every time and the clock drifts to ~22 gen/s; carrying does not.
    [InlineData(0.0469, 30, 0.0, 1, 0.407)]
    [InlineData(0.0469, 30, 0.407, 1, 0.814)]
    [InlineData(0.0469, 30, 0.814, 2, 0.221)]
    [InlineData(0.0167, 60, 0.0, 1, 0.002)]
    [InlineData(0.0010, 1000, 0.0, 1, 0.0)]
    [InlineData(5.0, 30, 0.0, 16, 1.0)]     // backlog is dropped, not queued
    [InlineData(0.0, 30, 0.5, 0, 0.5)]      // nothing new this tick
    public void Budget_carries_the_fractional_generation(double elapsed, int target, double carry,
                                                          int expectedSteps, double expectedCarry)
    {
        var (steps, next) = SimClock.BudgetFor(elapsed, target, carry);
        Assert.Equal(expectedSteps, steps);
        Assert.Equal(expectedCarry, next, 3);
    }

    [Fact]
    public void Carried_budget_hits_the_target_rate_over_many_ticks()
    {
        // Simulate the OS timer grid: ticks arrive at 47 ms, never 33 ms.
        var carry = 0.0;
        long total = 0;
        for (var i = 0; i < 100; ++i)
        {
            var (steps, next) = SimClock.BudgetFor(0.047, 30, carry);
            carry = next;
            total += steps;
        }
        // 100 * 47 ms = 4.7 s at 30 gen/s = 141 generations. Naive rounding
        // would have produced 100.
        Assert.InRange(total, 138, 144);
    }

    [Fact]
    public async Task Dispose_stops_a_running_clock()
    {
        var world = new World(WorldConfig.Default(16, 16));
        var clock = new SimClock(world, 200);
        clock.Start();
        await Task.Delay(50);
        clock.Dispose();
        Assert.False(clock.IsRunning);
        var generation = world.Generation;
        await Task.Delay(150);
        Assert.Equal(generation, world.Generation);
    }
}
