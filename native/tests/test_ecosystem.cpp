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
