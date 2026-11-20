#include "check.h"

#include "core/species.h"
#include "ecosim_abi.h"

ECOSIM_TEST(species_table_is_contiguous_and_bounded) {
  CHECK_EQ_i32(3, ecosim::species_count());
  const ecosim::SpeciesDef* table = ecosim::species_table();
  for (int32_t i = 1; i <= ecosim::species_count(); ++i) CHECK_EQ_i32(i, table[i].id);
  CHECK_EQ_i32(0, table[0].id);
}

ECOSIM_TEST(species_invariants_hold) {
  for (int32_t i = 1; i <= ecosim::species_count(); ++i) {
    const ecosim::SpeciesDef& s = ecosim::species_of(i);
    CHECK_TRUE(s.initial_energy <= s.max_energy);
    CHECK_TRUE(s.metabolism > 0);          // nothing lives for free
    CHECK_TRUE(s.feed_efficiency_permille < 1000);  // no perpetual motion
    CHECK_TRUE(s.repro_threshold <= s.max_energy);
    CHECK_TRUE(s.mobility_permille <= 1000);
    CHECK_TRUE(s.mutation_permille <= 1000);
  }
  // Producers do not eat; they photosynthesise (regrow stage).
  CHECK_EQ_i32(0, ecosim::species_of(1).feed_efficiency_permille);
  CHECK_TRUE(ecosim::species_of(2).feed_efficiency_permille > 0);
  CHECK_TRUE(ecosim::species_of(3).feed_efficiency_permille >
             ecosim::species_of(2).feed_efficiency_permille);
}

ECOSIM_TEST(unknown_species_falls_back_to_empty) {
  CHECK_EQ_i32(0, ecosim::species_of(99).id);
  CHECK_EQ_i32(0, ecosim::species_of(-3).id);
}

ECOSIM_TEST(randomize_and_clear_seed_the_initial_energy) {
  EcosimConfig cfg{};
  cfg.struct_size = sizeof(EcosimConfig);
  cfg.w = 32;
  cfg.h = 32;
  cfg.mode = ECOSIM_MODE_BIRSURV;
  cfg.topology = ECOSIM_TOPOLOGY_TORUS;
  cfg.threads = 1;
  cfg.birth_mask[3] = 1;
  cfg.survive_mask[2] = 1;
  cfg.survive_mask[3] = 1;
  EcosimWorld* w = ecosim_world_create(&cfg);

  EcosimSeedConfig seed{};
  seed.struct_size = sizeof(seed);
  seed.species = 2;
  seed.density_permille = 400;
  seed.seed = 11;
  CHECK_EQ_i32(ECOSIM_OK, ecosim_randomize(w, &seed));

  EcosimGridView view{};
  view.struct_size = sizeof(view);
  CHECK_EQ_i32(ECOSIM_OK, ecosim_world_grid_view(w, &view));
  const uint16_t want = ecosim::species_of(2).initial_energy;
  int32_t alive = 0;
  for (size_t i = 0; i < 32u * 32u; ++i) {
    if (view.species[i] == 0) {
      CHECK_EQ_i32(0, view.energy[i]);
    } else {
      ++alive;
      CHECK_EQ_i32(want, view.energy[i]);
    }
  }
  CHECK_TRUE(alive > 0);

  CHECK_EQ_i32(ECOSIM_OK, ecosim_clear(w, 3));
  CHECK_EQ_i32(ECOSIM_OK, ecosim_world_grid_view(w, &view));
  for (size_t i = 0; i < 32u * 32u; ++i) {
    CHECK_EQ_i32(ecosim::species_of(3).initial_energy, view.energy[i]);
  }

  ecosim_world_destroy(w);
}
