#include "check.h"

#include <cmath>
#include <cstring>
#include <vector>

#include "ecosim_abi.h"

namespace {

EcosimWorld* make_world(int32_t w, int32_t h, int32_t threads = 1) {
  EcosimConfig cfg{};
  cfg.struct_size = sizeof(EcosimConfig);
  cfg.w = w;
  cfg.h = h;
  cfg.mode = ECOSIM_MODE_BIRSURV;
  cfg.topology = ECOSIM_TOPOLOGY_TORUS;
  cfg.threads = threads;
  cfg.birth_mask[3] = 1;
  cfg.survive_mask[2] = 1;
  cfg.survive_mask[3] = 1;
  cfg.rng_seed = 5;
  return ecosim_world_create(&cfg);
}

EcosimStats read_stats(EcosimWorld* w) {
  EcosimStats stats{};
  stats.struct_size = sizeof(EcosimStats);
  CHECK_EQ_i32(ECOSIM_OK, ecosim_stats(w, &stats));
  return stats;
}

uint64_t randomize_and_hash(EcosimWorld* w, int32_t species, uint32_t density, uint64_t seed) {
  EcosimSeedConfig seed_cfg{};
  seed_cfg.struct_size = sizeof(EcosimSeedConfig);
  seed_cfg.species = species;
  seed_cfg.density_permille = density;
  seed_cfg.seed = seed;
  CHECK_EQ_i32(ECOSIM_OK, ecosim_randomize(w, &seed_cfg));
  return ecosim_hash(w);
}

}  // namespace

ECOSIM_TEST(paint_rect_is_half_open_and_clamped) {
  EcosimWorld* w = make_world(8, 8);
  EcosimPaintOp op{};
  op.struct_size = sizeof(op);
  op.x0 = 2;
  op.y0 = 2;
  op.x1 = 4;  // columns 2 and 3 only
  op.y1 = 3;
  op.species = 1;
  CHECK_EQ_i32(ECOSIM_OK, ecosim_paint(w, &op, 1));
  EcosimStats stats = read_stats(w);
  CHECK_EQ_i32(2, stats.alive_count);

  op.x1 = 9;  // past the edge
  CHECK_EQ_i32(ECOSIM_ERR_RANGE, ecosim_paint(w, &op, 1));
  CHECK_TRUE(std::strlen(ecosim_last_error()) > 0);

  op.x1 = 4;
  op.y0 = 5;
  op.y1 = 4;  // empty rectangle
  CHECK_EQ_i32(ECOSIM_ERR_RANGE, ecosim_paint(w, &op, 1));

  CHECK_EQ_i32(ECOSIM_ERR_INVALID, ecosim_paint(w, nullptr, 1));
  CHECK_EQ_i32(ECOSIM_ERR_INVALID, ecosim_paint(w, &op, -1));
  ecosim_world_destroy(w);
}

ECOSIM_TEST(randomize_is_reproducible_per_seed) {
  EcosimWorld* a = make_world(128, 128);
  EcosimWorld* b = make_world(128, 128);
  const uint64_t ha = randomize_and_hash(a, 1, 300, 12345);
  const uint64_t hb = randomize_and_hash(b, 1, 300, 12345);
  CHECK_EQ_i64(static_cast<int64_t>(ha), static_cast<int64_t>(hb));

  const uint64_t hc = randomize_and_hash(a, 1, 300, 999);
  CHECK_TRUE(hc != ha);
  ecosim_world_destroy(a);
  ecosim_world_destroy(b);
}

ECOSIM_TEST(randomize_and_step_are_thread_count_independent) {
  // Appendix A.5.2: the draw is a function of (seed, generation, index), so a
  // different band split must not move a single cell.
  EcosimWorld* serial = make_world(256, 256, 1);
  EcosimWorld* parallel = make_world(256, 256, 4);
  const uint64_t h1 = randomize_and_hash(serial, 1, 300, 777);
  const uint64_t h2 = randomize_and_hash(parallel, 1, 300, 777);
  CHECK_EQ_i64(static_cast<int64_t>(h1), static_cast<int64_t>(h2));

  CHECK_EQ_i32(ECOSIM_OK, ecosim_world_step(serial, 10));
  CHECK_EQ_i32(ECOSIM_OK, ecosim_world_step(parallel, 10));
  CHECK_EQ_i64(static_cast<int64_t>(ecosim_hash(serial)),
               static_cast<int64_t>(ecosim_hash(parallel)));
  ecosim_world_destroy(serial);
  ecosim_world_destroy(parallel);
}

ECOSIM_TEST(randomize_density_within_tolerance) {
  EcosimWorld* w = make_world(256, 256);
  randomize_and_hash(w, 1, 250, 4242);
  const EcosimStats stats = read_stats(w);
  const double ratio = stats.alive_count / static_cast<double>(256 * 256);
  CHECK_TRUE(ratio > 0.22 && ratio < 0.28);

  EcosimSeedConfig bad{};
  bad.struct_size = sizeof(bad);
  bad.species = 1;
  bad.density_permille = 1001;
  CHECK_EQ_i32(ECOSIM_ERR_INVALID, ecosim_randomize(w, &bad));
  bad.density_permille = 500;
  bad.species = ECOSIM_MAX_SPECIES;
  CHECK_EQ_i32(ECOSIM_ERR_INVALID, ecosim_randomize(w, &bad));
  ecosim_world_destroy(w);
}

ECOSIM_TEST(stats_reports_generation_alive_and_per_species_counts) {
  EcosimWorld* w = make_world(16, 16);
  EcosimPaintOp ops[2]{};
  for (auto& op : ops) op.struct_size = sizeof(EcosimPaintOp);
  ops[0].x0 = 0;
  ops[0].y0 = 0;
  ops[0].x1 = 3;
  ops[0].y1 = 1;
  ops[0].species = 1;
  ops[1].x0 = 0;
  ops[1].y0 = 2;
  ops[1].x1 = 2;
  ops[1].y1 = 3;
  ops[1].species = 2;
  CHECK_EQ_i32(ECOSIM_OK, ecosim_paint(w, ops, 2));

  EcosimStats stats = read_stats(w);
  CHECK_EQ_i32(5, stats.alive_count);
  CHECK_EQ_i32(3, stats.species_count[1]);
  CHECK_EQ_i32(2, stats.species_count[2]);
  CHECK_EQ_i32(0, stats.species_count[3]);
  CHECK_EQ_i32(16, stats.w);
  CHECK_EQ_i32(16, stats.h);
  CHECK_EQ_i64(0, static_cast<int64_t>(stats.generation));

  CHECK_EQ_i32(ECOSIM_OK, ecosim_world_step(w, 4));
  stats = read_stats(w);
  CHECK_EQ_i64(4, static_cast<int64_t>(stats.generation));
  ecosim_world_destroy(w);
}

ECOSIM_TEST(clear_resets_the_board_but_keeps_the_generation) {
  EcosimWorld* w = make_world(16, 16);
  randomize_and_hash(w, 1, 400, 9);
  CHECK_EQ_i32(ECOSIM_OK, ecosim_world_step(w, 3));
  CHECK_EQ_i32(ECOSIM_OK, ecosim_clear(w, 0));
  const EcosimStats stats = read_stats(w);
  CHECK_EQ_i32(0, stats.alive_count);
  CHECK_EQ_i64(3, static_cast<int64_t>(stats.generation));

  CHECK_EQ_i32(ECOSIM_OK, ecosim_clear(w, 2));
  const EcosimStats filled = read_stats(w);
  CHECK_EQ_i32(256, filled.alive_count);
  CHECK_EQ_i32(256, filled.species_count[2]);

  CHECK_EQ_i32(ECOSIM_ERR_INVALID, ecosim_clear(w, ECOSIM_MAX_SPECIES));
  CHECK_EQ_i32(ECOSIM_ERR_INVALID, ecosim_clear(w, -1));
  ecosim_world_destroy(w);
}
