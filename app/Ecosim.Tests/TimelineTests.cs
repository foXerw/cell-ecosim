using Ecosim.Core;

namespace Ecosim.Tests;

public class TimelineTests
{
    private static World Ecosystem(int size = 16, ulong seed = 13)
    {
        var world = new World(WorldConfig.Default(size, size) with { Mode = RuleMode.Ecosystem });
        world.Randomize(new SeedConfig { Species = 1, DensityPermille = 300, Seed = seed });
        return world;
    }

    [Fact]
    public void The_timeline_tracks_the_engine_and_starts_live()
    {
        using var world = Ecosystem();
        var timeline = new TimelineModel(world);
        Assert.Equal(-1, timeline.Index);
        Assert.False(timeline.IsReviewing);

        world.Step(12);
        timeline.Refresh();
        Assert.Equal(12, timeline.Count);
        Assert.Equal(world.Generation, timeline.CurrentGeneration);  // still live
    }

    [Fact]
    public void Restoring_rewinds_the_generation_counter()
    {
        using var world = Ecosystem();
        world.Step(20);
        var timeline = new TimelineModel(world);

        Assert.True(timeline.Restore(4));
        Assert.True(timeline.IsReviewing);
        Assert.Equal(5, world.Generation);
        Assert.Equal(world.Generation, timeline.CurrentGeneration);
    }

    [Fact]
    public void Out_of_range_indices_are_refused_not_thrown()
    {
        using var world = Ecosystem();
        world.Step(6);
        var timeline = new TimelineModel(world);

        Assert.False(timeline.Restore(-1));
        Assert.False(timeline.Restore(timeline.Count));
        Assert.False(timeline.IsReviewing);

        timeline.Index = 999;
        Assert.Equal(timeline.Count - 1, timeline.Index);
    }

    [Fact]
    public void Exiting_review_returns_to_live()
    {
        using var world = Ecosystem();
        world.Step(8);
        var timeline = new TimelineModel(world);
        timeline.Restore(2);

        timeline.ExitReview();
        Assert.False(timeline.IsReviewing);
        Assert.Equal(-1, timeline.Index);

        // Reviewing does not freeze the board: stepping on from a restored
        // snapshot continues the simulation from that generation.
        world.Step(3);
        Assert.Equal(6, world.Generation);
    }

    [Fact]
    public void Rewind_and_replay_reproduces_the_same_board()
    {
        // The property the scrubber depends on: because the engine rewinds the
        // generation with the board and the rng is stateless over
        // (seed, generation, cell), replaying is exact rather than approximate.
        using var neverLeft = Ecosystem();
        neverLeft.Step(10);
        neverLeft.SnapshotCapture();
        neverLeft.Step(15);
        var expected = neverLeft.Hash();

        using var rewound = Ecosystem();
        rewound.Step(25);
        var timeline = new TimelineModel(rewound);

        // The dense window keeps one frame per generation, so generation 10 is
        // there; find it rather than assuming its index.
        var index = Enumerable.Range(0, timeline.Count)
                              .First(i => rewound.SnapshotGeneration(i) == 10);
        Assert.True(timeline.Restore(index));
        Assert.Equal(10, rewound.Generation);

        rewound.Step(15);
        Assert.Equal(expected, rewound.Hash());
    }

    [Fact]
    public void Capture_pins_a_frame_anytime()
    {
        using var world = Ecosystem();
        world.Step(3);
        var timeline = new TimelineModel(world);
        var before = timeline.Count;

        Assert.True(world.SnapshotCapture() >= 0);
        timeline.Refresh();
        Assert.Equal(before + 1, timeline.Count);
        Assert.True(timeline.Restore(timeline.Count - 1));
        Assert.Equal(3, world.Generation);
    }
}
