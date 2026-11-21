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
  b.step(40);

  CHECK_EQ_i32(ECOSIM_OK, b.restore_snapshot(0));
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
  CHECK_EQ_i32(ecosim::SnapshotRing::kDefaultSlots, w.snapshots().count());
  CHECK_TRUE(w.snapshots().generation_at(63) - w.snapshots().generation_at(0) > 300);

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
  EcosimWorld* w = ecosim_world_create(&eco8());
  CHECK_EQ_i32(ECOSIM_OK, ecosim_randomize(w, &soup(1, 300, 7)));
  CHECK_EQ_i32(ECOSIM_OK, ecosim_world_step(w, 6));

  const int32_t count = ecosim_snapshot_count(w);
  CHECK_TRUE(count >= 6);

  uint64_t generation = 0;
  CHECK_EQ_i32(ECOSIM_OK, ecosim_snapshot_info(w, count - 1, &generation));
  CHECK_EQ_i64(6, static_cast<int64_t>(generation));
  CHECK_EQ_i32(ECOSIM_ERR_RANGE, ecosim_snapshot_info(w, count, &generation));
  CHECK_EQ_i32(ECOSIM_ERR_INVALID, ecosim_snapshot_info(w, 0, nullptr));

  const int32_t pinned = ecosim_snapshot_capture(w);
  CHECK_TRUE(pinned >= 0);
  CHECK_EQ_i32(ECOSIM_OK, ecosim_snapshot_restore(w, pinned));
  CHECK_EQ_i32(ECOSIM_ERR_INVALID, ecosim_snapshot_restore(nullptr, 0));

  ecosim_world_destroy(w);
}
