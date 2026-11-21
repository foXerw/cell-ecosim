#include "check.h"

#include <cstring>
#include <vector>

#include "core/snapshots.h"
#include "core/world.h"
#include "ecosim_abi.h"

namespace {

EcosimConfig eco8() {
  EcosimConfig cfg{};
  cfg.struct_size = sizeof(cfg);
  cfg.w = 24;
  cfg.h = 24;
  cfg.mode = ECOSIM_MODE_ECOSYSTEM;
  cfg.topology = ECOSIM_TOPOLOGY_TORUS;
  cfg.threads = 1;
  cfg.rng_seed = 17;
  return cfg;
}

EcosimSeedConfig soup(int32_t species, uint32_t density, uint64_t seed) {
  EcosimSeedConfig s{};
  s.struct_size = sizeof(s);
  s.species = species;
  s.density_permille = density;
  s.seed = seed;
  return s;
}

}  // namespace

ECOSIM_TEST(a_fresh_world_has_no_snapshots) {
  ecosim::World w(eco8());
  CHECK_EQ_i32(0, w.snapshots().count());
  CHECK_EQ_i32(ECOSIM_ERR_STATE, w.restore_snapshot(0));
}

ECOSIM_TEST(capture_and_restore_is_lossless_for_species_energy_resource) {
  ecosim::World w(eco8());
  CHECK_EQ_i32(ECOSIM_OK, w.randomize(soup(1, 300, 5)));
  w.step(3);
  const auto before = w.grid().cur();

  CHECK_TRUE(w.snapshots().capture(w.generation(), w.grid()) >= 0);
  w.step(25);
  CHECK_EQ_i32(ECOSIM_OK, w.restore_snapshot(0));

  const auto& after = w.grid().cur();
  CHECK_TRUE(before.species == after.species);
  CHECK_TRUE(before.energy == after.energy);
  CHECK_TRUE(before.resource == after.resource);
  // Age is not stored (appendix A.3): it comes back as zero, not as garbage.
  for (size_t i = 0; i < after.age.size(); ++i) {
    if (after.species[i] != 0) CHECK_EQ_i32(0, after.age[i]);
  }
  CHECK_EQ_i64(3, static_cast<int64_t>(w.generation()));
}

ECOSIM_TEST(restoring_and_stepping_forward_reproduces_the_later_board) {
  // The real acceptance for a scrubber: rewinding and replaying must land on
  // the same state as never having left. That only works because the
  // generation counter rewinds with the board, so the stateless rng draws the
  // same values (appendix A.5.2).
  ecosim::World a(eco8());
  CHECK_EQ_i32(ECOSIM_OK, a.randomize(soup(1, 300, 11)));
  a.step(10);
  a.snapshots().capture(a.generation(), a.grid());
  a.step(20);

  ecosim::World b(eco8());
  CHECK_EQ_i32(ECOSIM_OK, b.randomize(soup(1, 300, 11)));
  // Same history as a, including the pinned frame at generation 10, then it
  // runs on past the point a stopped.
  b.step(10);
  b.snapshots().capture(b.generation(), b.grid());
  b.step(30);

  // The ring samples every 8th generation, so index 0 is generation 8 and the
  // manually pinned generation 10 sits later. Find it rather than assume it.
  int32_t at_ten = -1;
  for (int32_t i = 0; i < b.snapshots().count(); ++i) {
    if (b.snapshots().generation_at(i) == 10) at_ten = i;
  }
  CHECK_TRUE(at_ten >= 0);
  CHECK_EQ_i32(ECOSIM_OK, b.restore_snapshot(at_ten));
  CHECK_EQ_i64(10, static_cast<int64_t>(b.generation()));
  b.step(20);

  CHECK_TRUE(a.grid().cur().species == b.grid().cur().species);
  CHECK_TRUE(a.grid().cur().energy == b.grid().cur().energy);
}

ECOSIM_TEST(the_ring_widens_its_stride_instead_of_growing_unbounded) {
  ecosim::World w(eco8());
  CHECK_EQ_i32(ECOSIM_OK, w.randomize(soup(1, 300, 3)));
  w.step(40);
  // Sampled, not per generation: every 8th generation, because capturing costs
  // more than a generation of simulation at these board sizes.
  CHECK_EQ_i32(5, w.snapshots().count());
  CHECK_EQ_i64(8, static_cast<int64_t>(w.snapshots().generation_at(0)));
  CHECK_EQ_i64(40, static_cast<int64_t>(w.snapshots().generation_at(4)));

  w.step(300);
  const int32_t count = w.snapshots().count();
  CHECK_TRUE(count <= ecosim::SnapshotRing::kDefaultSlots);
  // Past the initial 8-generation window the stride doubles, so the newest
  // frames are farther apart than the oldest ones. That widening is the whole
  // point: it is what keeps capture cost off the hot path.
  CHECK_TRUE(count > 32);
  const uint64_t first_gap = w.snapshots().generation_at(1) - w.snapshots().generation_at(0);
  const uint64_t last_gap = w.snapshots().generation_at(count - 1) -
                            w.snapshots().generation_at(count - 2);
  CHECK_TRUE(last_gap > first_gap);

  uint64_t previous = 0;
  for (int32_t i = 0; i < w.snapshots().count(); ++i) {
    const uint64_t gen = w.snapshots().generation_at(i);
    CHECK_TRUE(gen > previous);
    previous = gen;
  }
  // Decimated history must stay affordable: 64 frames of a 24x24 board.
  CHECK_TRUE(w.snapshots().bytes_used() < 64u * 24u * 24u * 4u);
}

ECOSIM_TEST(snapshot_abi_reports_and_refuses_sensibly) {
  const EcosimConfig config = eco8();
  EcosimWorld* w = ecosim_world_create(&config);
  const EcosimSeedConfig seed_cfg = soup(1, 300, 7);
  CHECK_EQ_i32(ECOSIM_OK, ecosim_randomize(w, &seed_cfg));
  // Sampling is every 8th generation, so six steps produce nothing at all -
  // a regression guard against quietly going back to per-generation capture.
  CHECK_EQ_i32(ECOSIM_OK, ecosim_world_step(w, 6));
  CHECK_EQ_i32(0, ecosim_snapshot_count(w));
  CHECK_EQ_i32(ECOSIM_OK, ecosim_world_step(w, 18));

  const int32_t count = ecosim_snapshot_count(w);
  CHECK_EQ_i32(3, count);

  uint64_t generation = 0;
  CHECK_EQ_i32(ECOSIM_OK, ecosim_snapshot_info(w, count - 1, &generation));
  CHECK_EQ_i64(24, static_cast<int64_t>(generation));
  CHECK_EQ_i32(ECOSIM_ERR_RANGE, ecosim_snapshot_info(w, count, &generation));
  CHECK_EQ_i32(ECOSIM_ERR_INVALID, ecosim_snapshot_info(w, 0, nullptr));

  const int32_t pinned = ecosim_snapshot_capture(w);
  CHECK_TRUE(pinned >= 0);
  CHECK_EQ_i32(ECOSIM_OK, ecosim_snapshot_restore(w, pinned));
  CHECK_EQ_i32(ECOSIM_ERR_INVALID, ecosim_snapshot_restore(nullptr, 0));

  ecosim_world_destroy(w);
}
