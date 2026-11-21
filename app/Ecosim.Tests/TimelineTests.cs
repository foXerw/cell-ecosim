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
        // Sampled every 8 generations, not one per generation: capturing costs
        // more than a generation of simulation at large boards.
        Assert.Equal(1, timeline.Count);
        Assert.Equal(8, world.SnapshotGeneration(0));
        Assert.Equal(world.Generation, timeline.CurrentGeneration);  // still live
    }

    [Fact]
    public void Restoring_rewinds_the_generation_counter()
    {
        using var world = Ecosystem();
        world.Step(20);
        var timeline = new TimelineModel(world);
        Assert.Equal(2, timeline.Count);

        Assert.True(timeline.Restore(0));
        Assert.True(timeline.IsReviewing);
        Assert.Equal(8, world.Generation);
        Assert.Equal(8, timeline.CurrentGeneration);
    }

    [Fact]
    public void Out_of_range_indices_are_refused_not_thrown()
    {
        using var world = Ecosystem();
        world.Step(10);
        var timeline = new TimelineModel(world);

        Assert.False(timeline.Restore(-1));
        Assert.False(timeline.Restore(timeline.Count));
        Assert.False(timeline.IsReviewing);

        timeline.Index = 999;
        Assert.Equal(timeline.Count - 1, timeline.Index);
    }

    [Fact]
    public void Exiting_review_returns_to_live_and_stepping_continues()
    {
        using var world = Ecosystem();
        world.Step(9);
        world.SnapshotCapture();          // pin generation 9 explicitly
        var timeline = new TimelineModel(world);
        timeline.Restore(timeline.Count - 1);

        timeline.ExitReview();
        Assert.False(timeline.IsReviewing);
        Assert.Equal(-1, timeline.Index);

        // Reviewing does not freeze the board: stepping on from a restored
        // snapshot continues the simulation from that generation.
        world.Step(3);
        Assert.Equal(12, world.Generation);
    }

    [Fact]
    public void Rewind_and_replay_reproduces_the_same_board()
    {
        // The property the scrubber depends on: because the engine rewinds the
        // generation with the board and the rng is stateless over
        // (seed, generation, cell), replaying is exact rather than approximate.
        // Generation 8 is sampled by the ring's stride, so both worlds can
        // reach the same frame without pinning by hand.
        using var neverLeft = Ecosystem();
        neverLeft.Step(8);
        var atEight = neverLeft.Hash();
        neverLeft.Step(12);
        var expected = neverLeft.Hash();

        using var rewound = Ecosystem();
        rewound.Step(30);
        var timeline = new TimelineModel(rewound);
        var index = Enumerable.Range(0, timeline.Count)
                              .First(i => rewound.SnapshotGeneration(i) == 8);

        Assert.True(timeline.Restore(index));
        Assert.Equal(8, rewound.Generation);
        Assert.Equal(atEight, rewound.Hash());

        rewound.Step(12);
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

    [Fact]
    public void The_scrubber_rests_at_zero_while_live_and_shows_the_index_while_reviewing()
    {
        // The slider is bound OneWay to this value, so "live" has to be a real
        // number rather than the index of whatever frame was last reviewed:
        // a stale index made the thumb sit mid-track while the board ran.
        using var world = Ecosystem();
        world.Step(24);
        var timeline = new TimelineModel(world);
        Assert.True(timeline.Count > 1);

        Assert.Equal(0, timeline.ScrubValue);

        Assert.True(timeline.Restore(timeline.Count - 1));
        Assert.Equal(timeline.Count - 1, timeline.ScrubValue);

        timeline.ExitReview();
        Assert.Equal(0, timeline.ScrubValue);
    }

    [Fact]
    public void A_growing_ring_does_not_drag_a_live_timeline_into_review()
    {
        // Maximum is bound to MaximumIndex, so it moves while the simulation
        // runs. Growing must not read back as a selection: the timeline stays
        // live and keeps reporting the engine's own generation.
        using var world = Ecosystem();
        var timeline = new TimelineModel(world);
        Assert.Equal(0, timeline.MaximumIndex);

        var raised = new List<string?>();
        timeline.PropertyChanged += (_, e) => raised.Add(e.PropertyName);

        world.Step(24);
        timeline.Refresh();

        Assert.True(timeline.MaximumIndex > 0);
        Assert.Equal(-1, timeline.Index);
        Assert.False(timeline.IsReviewing);
        Assert.Equal(0, timeline.ScrubValue);
        Assert.Equal(world.Generation, timeline.CurrentGeneration);
        // Maximum is bound to MaximumIndex, so a Refresh that grew the ring
        // has to say so or the slider's range stays stuck at zero.
        Assert.Contains(nameof(TimelineModel.MaximumIndex), raised);
    }

    [Fact]
    public void Refreshing_while_reviewing_keeps_the_reviewed_frame()
    {
        // The app refreshes the timeline on every pump tick, including while the
        // user is holding the scrubber. A refresh must not knock them back to
        // live or move the thumb off the frame they picked - and because the
        // engine keeps stepping, the ring really does grow underneath them here.
        using var world = Ecosystem();
        world.Step(16);
        var timeline = new TimelineModel(world);
        var picked = 0;
        Assert.True(timeline.Restore(picked));
        var generation = timeline.CurrentGeneration;

        world.Step(16);
        timeline.Refresh();

        Assert.True(timeline.IsReviewing);
        Assert.Equal(picked, timeline.Index);
        Assert.Equal(picked, timeline.ScrubValue);
        Assert.Equal(generation, timeline.CurrentGeneration);

        // Leaving review afterwards still lands on live.
        timeline.ExitReview();
        Assert.Equal(-1, timeline.Index);
        Assert.Equal(0, timeline.ScrubValue);
    }
}
