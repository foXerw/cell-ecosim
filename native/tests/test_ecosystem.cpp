#include "check.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "core/grid.h"
#include "core/kernel.h"
#include "core/params.h"
#include "core/species.h"
#include "core/world.h"
#include "ecosim_abi.h"
#include "rules/pipeline.h"
#include "util/rng.h"

namespace {

EcosimConfig eco_config(int32_t w, int32_t h, int32_t threads = 1) {
  EcosimConfig cfg{};
  cfg.struct_size = sizeof(EcosimConfig);
  cfg.w = w;
  cfg.h = h;
  cfg.mode = ECOSIM_MODE_ECOSYSTEM;
  cfg.topology = ECOSIM_TOPOLOGY_BOUNDED;
  cfg.threads = threads;
  cfg.rng_seed = 1234;
  return cfg;
}

/// Food webs are defined on an unbounded board: with a bounded one, grazers
/// walk off the edge and the web collapses, which is a topology artefact and
/// not what these scenarios are about.
EcosimConfig eco_torus(int32_t n) {
  EcosimConfig cfg = eco_config(n, n);
  cfg.topology = ECOSIM_TOPOLOGY_TORUS;
  return cfg;
}

struct Eco {
  ecosim::World w;
  explicit Eco(int32_t size) : w(eco_config(size, size)) {}

  void fill(int32_t species) { CHECK_EQ_i32(ECOSIM_OK, w.clear(species)); }

  void paint(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t species) {
    EcosimPaintOp op{};
    op.struct_size = sizeof(op);
    op.x0 = x0;
    op.y0 = y0;
    op.x1 = x1;
    op.y1 = y1;
    op.species = species;
    CHECK_EQ_i32(ECOSIM_OK, w.paint(&op, 1));
  }

  const ecosim::Planes& cur() const { return w.grid().cur(); }

  uint16_t energy(int32_t x, int32_t y) const { return cur().energy[w.grid().index(x, y)]; }
  uint8_t species(int32_t x, int32_t y) const { return cur().species[w.grid().index(x, y)]; }
  uint8_t resource(int32_t x, int32_t y) const { return cur().resource[w.grid().index(x, y)]; }

  /// Energies of every living cell, sorted: robust against dispersal moving
  /// cells around, which the hand-computed scenarios must not depend on.
  std::vector<int32_t> energy_multiset() const {
    std::vector<int32_t> out;
    for (size_t i = 0; i < cur().species.size(); ++i) {
      if (cur().species[i] != 0) out.push_back(cur().energy[i]);
    }
    std::sort(out.begin(), out.end());
    return out;
  }
};

}  // namespace

ECOSIM_TEST(sense_counts_neighbours_per_species_and_sums_resource) {
  ecosim::Grid g;
  g.allocate(5, 5);
  g.cur().species[g.index(2, 2)] = 1;
  g.cur().species[g.index(1, 0)] = 2;
  g.cur().species[g.index(0, 0)] = 3;
  g.cur().resource[g.index(2, 2)] = 10;
  g.cur().resource[g.index(1, 0)] = 20;

  ecosim::SenseField sf;
  ecosim::sense(g.cur(), 5, 5, ECOSIM_TOPOLOGY_BOUNDED, sf);

  // Neighbours of (1,1) are (0,0)=hunter, (1,0)=grazer and (2,2)=alga on the
  // diagonal - all three count, and the resource under two of them sums to 30.
  const size_t i = g.index(1, 1);
  CHECK_EQ_i32(1, sf.count[0][i]);
  CHECK_EQ_i32(1, sf.count[1][i]);
  CHECK_EQ_i32(1, sf.count[2][i]);
  CHECK_EQ_i32(30, sf.resource_sum[i]);

  const size_t corner = g.index(4, 4);
  CHECK_EQ_i32(0, sf.count[0][corner]);
  CHECK_EQ_i32(0, sf.resource_sum[corner]);
}

ECOSIM_TEST(feed_transfers_exactly_the_efficiency_share) {
  // 5x5 of algae (energy 80 each), one grazer dropped in the middle. The board
  // is full, so nothing can disperse and positions stay put.
  Eco e(5);
  e.fill(1);
  e.paint(2, 2, 3, 3, 2);

  CHECK_EQ_i32(ECOSIM_OK, e.w.step(1));

  // Grazer took the first alga in scan order, (1,1): gain = 80*500/1000 = 40,
  // then burned its metabolism of 3. The prey lost 40 and burned 1.
  CHECK_EQ_i32(80 + 40 - 3, e.energy(2, 2));
  CHECK_EQ_i32(80 - 40 - 1, e.energy(1, 1));
  CHECK_EQ_i32(80 - 1, e.energy(4, 4));  // untouched alga
  CHECK_EQ_i32(1, e.species(1, 1));      // nibbled, not killed
}

ECOSIM_TEST(producers_photosynthesise_from_surrounding_resource) {
  Eco e(3);
  e.fill(1);  // algae start at 80 energy
  // Every cell gets 100 resource: the centre sees a sum of 800, a corner 300.
  for (size_t i = 0; i < e.cur().resource.size(); ++i) {
    e.w.grid().cur().resource[i] = 100;
  }
  e.w.grid().copy_cur_to_nxt();

  CHECK_EQ_i32(ECOSIM_OK, e.w.step(1));
  // Centre: gain = 800*350/8000 = 35, drawn from its own stock; then burn 1.
  CHECK_EQ_i32(80 + 35 - 1, e.energy(1, 1));
  CHECK_EQ_i32(65, e.resource(1, 1));
  // Corner: gain = 300*350/8000 = 13.
  CHECK_EQ_i32(80 + 13 - 1, e.energy(0, 0));
  CHECK_EQ_i32(87, e.resource(0, 0));
}

ECOSIM_TEST(starvation_kills_on_schedule_and_leaves_a_carcass) {
  Eco e(3);
  e.fill(2);
  EcosimParams p = ecosim::default_params();
  p.fertility_permille = 0;  // keep the carcass count exact
  CHECK_EQ_i32(ECOSIM_OK, e.w.set_params(p));

  CHECK_EQ_i32(ECOSIM_OK, e.w.step(49));
  CHECK_EQ_i32(150 - 3 * 49, e.energy(1, 1));
  CHECK_EQ_i32(9, ecosim::kernel::count_alive(e.cur()));

  CHECK_EQ_i32(ECOSIM_OK, e.w.step(1));  // generation 50: 3 <= burn 3
  CHECK_EQ_i32(0, ecosim::kernel::count_alive(e.cur()));
  for (int32_t y = 0; y < 3; ++y) {
    for (int32_t x = 0; x < 3; ++x) {
      CHECK_EQ_i32(20, e.resource(x, y));
      CHECK_EQ_i32(0, e.species(x, y));
    }
  }
  CHECK_EQ_i32(ECOSIM_OK, e.w.step(200));  // a dead world stays boring
  CHECK_EQ_i32(0, ecosim::kernel::count_alive(e.cur()));
}

ECOSIM_TEST(reproduce_spends_a_share_on_one_child_each) {
  Eco e(4);
  e.fill(2);
  e.paint(0, 0, 4, 3, 0);  // leave only the bottom row alive: four grazers,
                           // each seeing 1-2 of its own, all with free space

  EcosimParams p = ecosim::default_params();
  p.repro_scale_permille[2] = 500;  // threshold 240 -> 120, under the 150 start
  p.fertility_permille = 0;
  CHECK_EQ_i32(ECOSIM_OK, e.w.set_params(p));

  CHECK_EQ_i32(ECOSIM_OK, e.w.step(1));

  // feed: nothing edible (no algae). metabolise: 150-3 = 147 each. reproduce:
  // all four clear the crowding limit and find free space, so each pays
  // 147*400/1000 = 58 and leaves a newborn with exactly that.
  const std::vector<int32_t> want = {58, 58, 58, 58, 89, 89, 89, 89};
  CHECK_TRUE(e.energy_multiset() == want);
}

ECOSIM_TEST(regrow_needs_light_and_resource) {
  {
    Eco e(8);
    e.fill(0);
    EcosimParams p = ecosim::default_params();
    p.light_permille = 0;
    CHECK_EQ_i32(ECOSIM_OK, e.w.set_params(p));
    CHECK_EQ_i32(ECOSIM_OK, e.w.step(300));
    CHECK_EQ_i32(0, ecosim::kernel::count_alive(e.cur()));
  }
  {
    Eco e(16);
    e.fill(0);
    CHECK_EQ_i32(ECOSIM_OK, e.w.step(4000));
    CHECK_TRUE(ecosim::kernel::count_alive(e.cur()) > 0);
  }
}

ECOSIM_TEST(ecosystem_result_does_not_depend_on_configured_threads) {
  ecosim::World a(eco_config(64, 64, 1));
  ecosim::World b(eco_config(64, 64, 4));

  EcosimSeedConfig seed{};
  seed.struct_size = sizeof(seed);
  seed.species = 2;  // grazers first: their carcasses feed the regrow stage,
  seed.density_permille = 250;  // so every pipeline stage gets exercised
  seed.seed = 77;
  CHECK_EQ_i32(ECOSIM_OK, a.randomize(seed));
  CHECK_EQ_i32(ECOSIM_OK, b.randomize(seed));

  CHECK_EQ_i32(ECOSIM_OK, a.step(60));
  CHECK_EQ_i32(ECOSIM_OK, b.step(60));
  CHECK_TRUE(a.grid().cur().species == b.grid().cur().species);
  CHECK_TRUE(a.grid().cur().energy == b.grid().cur().energy);
  CHECK_TRUE(a.grid().cur().resource == b.grid().cur().resource);
}

ECOSIM_TEST(sense_clears_between_calls_and_needs_no_preallocation) {
  // Regression guard: the census accumulates, so calling sense twice on the
  // same buffer must not double the counts, and a caller that forgot to
  // allocate must not be able to crash the engine.
  ecosim::Grid g;
  g.allocate(4, 4);
  g.cur().species[g.index(1, 1)] = 2;

  ecosim::SenseField sf;  // deliberately not allocated
  ecosim::sense(g.cur(), 4, 4, ECOSIM_TOPOLOGY_BOUNDED, sf);
  const size_t i = g.index(1, 0);
  const uint8_t first = sf.count[1][i];
  CHECK_EQ_i32(1, first);

  ecosim::sense(g.cur(), 4, 4, ECOSIM_TOPOLOGY_BOUNDED, sf);
  CHECK_EQ_i32(first, sf.count[1][i]);
  CHECK_EQ_i32(4, sf.w);
  CHECK_EQ_i32(4, sf.h);
}

namespace {

/// A mixed board seeded by the same stateless draw the engine uses, so the
/// scenario is reproducible without depending on randomize's single-species
/// semantics.
void seed_mixed(ecosim::World& w, uint64_t seed) {
  auto& planes = w.grid().cur();
  for (size_t i = 0; i < planes.species.size(); ++i) {
    const uint64_t r = ecosim::rng::draw(seed, 0, i);
    const uint32_t permille = r % 1000u;
    uint8_t s = 0;
    if (permille < 250) s = 1;
    else if (permille < 290) s = 2;
    else if (permille < 296) s = 3;
    planes.species[i] = s;
    planes.energy[i] = s ? ecosim::species_of(s).initial_energy : 0;
    planes.age[i] = 0;
    planes.resource[i] = static_cast<uint8_t>((r >> 20) % 60u);
  }
  w.grid().copy_cur_to_nxt();
}

std::vector<EcosimStats> sample_series(ecosim::World& w, int32_t gens, int32_t every) {
  std::vector<EcosimStats> series;
  for (int32_t g = 0; g <= gens; g += every) {
    if (g > 0) CHECK_EQ_i32(ECOSIM_OK, w.step(every));
    EcosimStats st{};
    st.struct_size = sizeof(st);
    CHECK_EQ_i32(ECOSIM_OK, w.stats(st));
    series.push_back(st);
  }
  return series;
}

/// Indices of local maxima with the given half-window.
std::vector<size_t> peaks(const std::vector<EcosimStats>& series, int32_t species,
                          int32_t window) {
  std::vector<size_t> out;
  for (size_t i = window; i + window < series.size(); ++i) {
    const int32_t v = series[i].species_count[species];
    if (v <= 0) continue;
    bool is_peak = true;
    for (int32_t k = -window; k <= window && is_peak; ++k) {
      if (k == 0) continue;
      if (series[i + k].species_count[species] >= v) is_peak = false;
    }
    if (is_peak) out.push_back(i);
  }
  return out;
}

}  // namespace

ECOSIM_TEST(ecosystem_sustains_three_species_for_2000_generations) {
  // The M2 exit criterion: this is not a Life board with extra fields, the
  // food web has to keep running.
  ecosim::World w(eco_torus(128));
  seed_mixed(w, 42);
  const auto series = sample_series(w, 2000, 20);

  const auto& last = series.back();
  CHECK_TRUE(last.species_count[1] > 0);
  CHECK_TRUE(last.species_count[2] > 0);
  CHECK_TRUE(last.species_count[3] > 0);

  int32_t zero_grazers = 0;
  int32_t zero_hunters = 0;
  for (const auto& st : series) {
    zero_grazers += st.species_count[2] == 0;
    zero_hunters += st.species_count[3] == 0;
  }
  CHECK_EQ_i32(0, zero_grazers);
  CHECK_EQ_i32(0, zero_hunters);
}

ECOSIM_TEST(predator_peaks_follow_prey_peaks) {
  // Lotka-Volterra signature: grazers peak, hunters peak some time later, and
  // this repeats. A board that merely survives is not enough.
  ecosim::World w(eco_torus(128));
  seed_mixed(w, 42);
  const auto series = sample_series(w, 1200, 20);

  const auto grazer_peaks = peaks(series, 2, 2);
  const auto hunter_peaks = peaks(series, 3, 2);
  CHECK_TRUE(grazer_peaks.size() >= 2);
  CHECK_TRUE(hunter_peaks.size() >= 1);

  int32_t lagged = 0;
  for (size_t hp : hunter_peaks) {
    for (size_t gp : grazer_peaks) {
      const int32_t lag_gens = static_cast<int32_t>((hp - gp) * 20);
      if (lag_gens >= 0 && lag_gens <= 120) ++lagged;
    }
  }
  CHECK_TRUE(lagged >= 1);
}

ECOSIM_TEST(ecosystem_survives_across_seeds) {
  // One lucky seed would be a coincidence, not a model.
  for (const uint64_t seed : {7ull, 101ull, 999ull}) {
    ecosim::World w(eco_torus(96));
    seed_mixed(w, seed);
    w.step(1200);
    EcosimStats st{};
    st.struct_size = sizeof(st);
    CHECK_EQ_i32(ECOSIM_OK, w.stats(st));
    CHECK_TRUE(st.species_count[2] > 0);
    CHECK_TRUE(st.species_count[3] > 0);
  }
}

ECOSIM_TEST(load_species_moves_cells_losslessly_and_reseeds_energy) {
  ecosim::World w(eco_torus(4));
  EcosimSeedConfig seed{};
  seed.struct_size = sizeof(seed);
  seed.species = 2;
  seed.density_permille = 400;
  seed.seed = 9;
  CHECK_EQ_i32(ECOSIM_OK, w.randomize(seed));

  const uint8_t plane[16] = {
      1, 1, 0, 0,   //
      1, 2, 2, 0,   //
      0, 2, 3, 3,   //
      0, 0, 3, 0,   //
  };
  CHECK_EQ_i32(ECOSIM_OK, w.load_species(plane, 16));

  const auto& cur = w.grid().cur();
  for (size_t i = 0; i < 16; ++i) {
    CHECK_EQ_i32(plane[i], cur.species[i]);
    CHECK_EQ_i32(0, cur.age[i]);
    const uint16_t want = plane[i] ? ecosim::species_of(plane[i]).initial_energy : 0;
    CHECK_EQ_i32(want, cur.energy[i]);
  }

  CHECK_EQ_i32(ECOSIM_ERR_RANGE, w.load_species(plane, 15));
  CHECK_EQ_i32(ECOSIM_ERR_INVALID, w.load_species(nullptr, 16));
  CHECK_TRUE(std::strlen(ecosim_last_error()) > 0);
}
