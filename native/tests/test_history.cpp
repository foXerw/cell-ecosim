#include "check.h"

#include <vector>

#include "core/history.h"
#include "core/world.h"
#include "ecosim_abi.h"

namespace {

EcosimConfig cfg_for(int32_t w, int32_t h, int32_t mode) {
  EcosimConfig cfg{};
  cfg.struct_size = sizeof(EcosimConfig);
  cfg.w = w;
  cfg.h = h;
  cfg.mode = mode;
  cfg.topology = ECOSIM_TOPOLOGY_TORUS;
  cfg.threads = 1;
  cfg.birth_mask[3] = 1;
  cfg.survive_mask[2] = 1;
  cfg.survive_mask[3] = 1;
  cfg.rng_seed = 5;
  return cfg;
}

}  // namespace

ECOSIM_TEST(history_ring_is_empty_on_a_fresh_world) {
  ecosim::World w(cfg_for(8, 8, ECOSIM_MODE_BIRSURV));
  CHECK_EQ_i32(0, w.history().size());
  CHECK_EQ_i32(ECOSIM_HISTORY_CAPACITY, w.history().capacity());
}

ECOSIM_TEST(history_keeps_one_sample_per_generation_in_order) {
  ecosim::World w(cfg_for(32, 32, ECOSIM_MODE_BIRSURV));
  EcosimSeedConfig seed{};
  seed.struct_size = sizeof(seed);
  seed.species = 1;
  seed.density_permille = 300;
  seed.seed = 8;
  CHECK_EQ_i32(ECOSIM_OK, w.randomize(seed));
  CHECK_EQ_i32(ECOSIM_OK, w.step(50));

  CHECK_EQ_i32(50, w.history().size());
  CHECK_EQ_i64(1, static_cast<int64_t>(w.history().at(0).generation));
  CHECK_EQ_i64(50, static_cast<int64_t>(w.history().at(49).generation));

  // The census must match a plain recount of the board at the last sample.
  EcosimStats stats{};
  stats.struct_size = sizeof(stats);
  CHECK_EQ_i32(ECOSIM_OK, w.stats(stats));
  CHECK_EQ_i32(stats.alive_count, w.history().at(49).alive);
  for (int32_t s = 0; s < ECOSIM_MAX_SPECIES; ++s) {
    CHECK_EQ_i32(stats.species_count[s], w.history().at(49).counts[s]);
  }
}

ECOSIM_TEST(history_ring_wraps_and_keeps_the_newest) {
  ecosim::HistoryRing ring(8);
  for (int32_t g = 1; g <= 30; ++g) {
    ecosim::SpeciesCounts counts{};
    counts[1] = g;
    ring.push(static_cast<uint64_t>(g), counts);
  }
  CHECK_EQ_i32(8, ring.size());
  CHECK_EQ_i64(23, static_cast<int64_t>(ring.at(0).generation));
  CHECK_EQ_i64(30, static_cast<int64_t>(ring.at(7).generation));
  CHECK_EQ_i32(30, ring.at(7).counts[1]);
  CHECK_EQ_i32(23, ring.at(0).counts[1]);

  ring.clear();
  CHECK_EQ_i32(0, ring.size());
}

ECOSIM_TEST(history_records_the_ecosystem_pipeline_too) {
  ecosim::World w(cfg_for(48, 48, ECOSIM_MODE_ECOSYSTEM));
  EcosimSeedConfig seed{};
  seed.struct_size = sizeof(seed);
  seed.species = 1;
  seed.density_permille = 300;
  seed.seed = 4;
  CHECK_EQ_i32(ECOSIM_OK, w.randomize(seed));
  CHECK_EQ_i32(ECOSIM_OK, w.step(20));

  CHECK_EQ_i32(20, w.history().size());
  const auto& last = w.history().at(19);
  EcosimStats stats{};
  stats.struct_size = sizeof(stats);
  CHECK_EQ_i32(ECOSIM_OK, w.stats(stats));
  CHECK_EQ_i32(stats.alive_count, last.alive);
  CHECK_EQ_i32(stats.species_count[1], last.counts[1]);
}

ECOSIM_TEST(history_abi_refuses_bad_indexes_and_sizes) {
  ecosim::World w(cfg_for(8, 8, ECOSIM_MODE_BIRSURV));
  CHECK_EQ_i32(ECOSIM_OK, w.step(3));

  EcosimHistorySample sample{};
  sample.struct_size = sizeof(sample);
  CHECK_EQ_i32(ECOSIM_ERR_INVALID, ecosim_history_get(nullptr, 0, &sample));
  CHECK_EQ_i32(0, ecosim_history_count(nullptr));
  CHECK_EQ_i32(ECOSIM_ERR_RANGE, ecosim_history_get(reinterpret_cast<EcosimWorld*>(&w), 3, &sample));
  CHECK_EQ_i32(ECOSIM_ERR_INVALID, ecosim_history_get(reinterpret_cast<EcosimWorld*>(&w), -1, &sample));
  CHECK_EQ_i32(ECOSIM_ERR_INVALID, ecosim_history_get(reinterpret_cast<EcosimWorld*>(&w), 0, nullptr));
  EcosimHistorySample tiny{};
  tiny.struct_size = 4;
  CHECK_EQ_i32(ECOSIM_ERR_SIZE, ecosim_history_get(reinterpret_cast<EcosimWorld*>(&w), 0, &tiny));
  CHECK_EQ_i32(3, ecosim_history_count(reinterpret_cast<EcosimWorld*>(&w)));
}
