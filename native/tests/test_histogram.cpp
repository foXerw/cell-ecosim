#include "check.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "core/history.h"
#include "core/species.h"
#include "core/world.h"
#include "ecosim_abi.h"

namespace {

EcosimConfig eco_config(int32_t w, int32_t h) {
  EcosimConfig cfg{};
  cfg.struct_size = sizeof(cfg);
  cfg.w = w;
  cfg.h = h;
  cfg.mode = ECOSIM_MODE_ECOSYSTEM;
  cfg.topology = ECOSIM_TOPOLOGY_TORUS;
  cfg.threads = 1;
  cfg.rng_seed = 23;
  return cfg;
}

/// Count the buckets by hand from the planes: the slow way, which is exactly
/// what the incremental numbers have to agree with.
ecosim::EnergyHistogram recount(const ecosim::World& w) {
  ecosim::EnergyHistogram hist{};
  hist.fill(0);
  const auto& cur = w.grid().cur();
  for (size_t i = 0; i < cur.species.size(); ++i) {
    if (cur.species[i] == 0) continue;
    const int32_t bucket = ecosim::energy_bucket(cur.species[i], cur.energy[i]);
    if (bucket >= 0) ++hist[bucket];
  }
  return hist;
}

}  // namespace

ECOSIM_TEST(energy_bucket_is_normalised_per_species_and_saturates) {
  // 150 of 240 (alga) is a higher share than 150 of 400 (grazer): the ceiling
  // is per species, not a global constant.
  const int32_t alga = ecosim::energy_bucket(1, 150);
  const int32_t grazer = ecosim::energy_bucket(2, 150);
  CHECK_TRUE(alga > grazer);
  CHECK_EQ_i32(-1, ecosim::energy_bucket(0, 100));
  CHECK_EQ_i32(ecosim::kHistogramBuckets - 1, ecosim::energy_bucket(1, 65535));
  CHECK_TRUE(ecosim::energy_bucket(1, 0) == 0);
}

ECOSIM_TEST(the_live_histogram_matches_a_recount) {
  ecosim::World w(eco_config(24, 24));
  EcosimSeedConfig seed{};
  seed.struct_size = sizeof(seed);
  seed.species = 1;
  seed.density_permille = 300;
  seed.seed = 3;
  CHECK_EQ_i32(ECOSIM_OK, w.randomize(seed));

  for (int32_t round = 0; round < 5; ++round) {
    w.step(6);
    const auto expected = recount(w);
    for (int32_t i = 0; i < ecosim::kHistogramBuckets; ++i) {
      CHECK_EQ_i32(expected[i], w.last_histogram()[i]);
    }
  }
}

ECOSIM_TEST(the_histogram_survives_a_swap_and_counts_every_cell) {
  ecosim::World w(eco_config(16, 16));
  EcosimSeedConfig seed{};
  seed.struct_size = sizeof(seed);
  seed.species = 2;
  seed.density_permille = 250;
  seed.seed = 4;
  CHECK_EQ_i32(ECOSIM_OK, w.randomize(seed));
  w.step(3);

  int32_t total = 0;
  for (const int32_t bucket : w.last_histogram()) total += bucket;
  EcosimStats stats{};
  stats.struct_size = sizeof(stats);
  CHECK_EQ_i32(ECOSIM_OK, w.stats(stats));
  CHECK_EQ_i32(stats.alive_count, total);
}

ECOSIM_TEST(the_abi_folds_sixteen_buckets_into_the_requested_count) {
  EcosimConfig config = eco_config(16, 16);
  EcosimWorld* w = ecosim_world_create(&config);
  EcosimSeedConfig seed{};
  seed.struct_size = sizeof(seed);
  seed.species = 1;
  seed.density_permille = 300;
  seed.seed = 5;
  CHECK_EQ_i32(ECOSIM_OK, ecosim_randomize(w, &seed));
  CHECK_EQ_i32(ECOSIM_OK, ecosim_world_step(w, 4));

  std::vector<int32_t> twelve(12);
  CHECK_EQ_i32(ECOSIM_OK, ecosim_energy_histogram(w, 12, twelve.data(), 12));
  int32_t sum12 = 0;
  for (const int32_t v : twelve) sum12 += v;

  std::vector<int32_t> sixteen(16);
  CHECK_EQ_i32(ECOSIM_OK, ecosim_energy_histogram(w, 16, sixteen.data(), 16));
  int32_t sum16 = 0;
  for (const int32_t v : sixteen) sum16 += v;

  CHECK_EQ_i32(sum16, sum12);

  CHECK_EQ_i32(ECOSIM_ERR_RANGE, ecosim_energy_histogram(w, 1, twelve.data(), 12));
  CHECK_EQ_i32(ECOSIM_ERR_RANGE, ecosim_energy_histogram(w, 20, twelve.data(), 12));
  CHECK_EQ_i32(ECOSIM_ERR_INVALID, ecosim_energy_histogram(w, 8, nullptr, 8));
  CHECK_EQ_i32(ECOSIM_ERR_INVALID, ecosim_energy_histogram(nullptr, 8, twelve.data(), 12));

  ecosim_world_destroy(w);
}
