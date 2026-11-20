#include "check.h"

#include <cstring>
#include <vector>

#include "core/grid.h"
#include "core/kernel.h"
#include "core/kernel_fast.h"
#include "ecosim_abi.h"

namespace {

// Deterministic filler: the kernels are pure functions of their input planes,
// so a test-local LCG is enough and keeps the engine's own RNG out of it.
void fill_random(ecosim::Planes& p, int32_t w, int32_t h, uint64_t seed, int32_t density_permille) {
  uint64_t s = seed;
  for (int32_t y = 0; y < h; ++y) {
    for (int32_t x = 0; x < w; ++x) {
      s = s * 6364136223846793005ull + 1442695040888963407ull;
      const uint32_t v = static_cast<uint32_t>(s >> 32) % 1000u;
      p.species[static_cast<size_t>(y) * w + x] = v < static_cast<uint32_t>(density_permille) ? 1 : 0;
    }
  }
}

struct Pair {
  int32_t w;
  int32_t h;
  ecosim::Grid a;  // driven by the reference kernel
  ecosim::Grid b;  // driven by the fast kernel
};

void run_both(Pair& pair, int32_t gens, const ecosim::RuleTable& rules, int32_t topology) {
  for (int32_t i = 0; i < gens; ++i) {
    ecosim::kernel::step_birsurv(pair.a.cur(), pair.a.nxt(), pair.w, pair.h, rules, topology);
    pair.a.swap_buffers();
    ecosim::kernel::step_fast(pair.b.cur(), pair.b.nxt(), pair.w, pair.h, rules, topology);
    pair.b.swap_buffers();
  }
}

void seed_pair(Pair& pair, uint64_t seed, int32_t density) {
  pair.a.allocate(pair.w, pair.h);
  pair.b.allocate(pair.w, pair.h);
  fill_random(pair.a.cur(), pair.w, pair.h, seed, density);
  pair.a.copy_cur_to_nxt();
  fill_random(pair.b.cur(), pair.w, pair.h, seed, density);
  pair.b.copy_cur_to_nxt();
}

}  // namespace

ECOSIM_TEST(popcount8_table_matches_builtin_for_all_256) {
  for (int32_t v = 0; v < 256; ++v) {
    const uint8_t b = static_cast<uint8_t>(v);
    int32_t expected = 0;
    for (int32_t bit = 0; bit < 8; ++bit) expected += (b >> bit) & 1;
    CHECK_EQ_i32(expected, ecosim::kernel::popcount8(b));
  }
}

ECOSIM_TEST(neighbor_mask_bit_order_is_stable) {
  // Documented order: row-major over the eight offsets, self excluded.
  //   bit0 (-1,-1) bit1 (0,-1) bit2 (+1,-1)
  //   bit3 (-1, 0)            bit4 (+1, 0)
  //   bit5 (-1,+1) bit6 (0,+1) bit7 (+1,+1)
  ecosim::Grid g;
  g.allocate(3, 3);
  for (size_t i = 0; i < 9; ++i) g.cur().species[i] = 1;

  CHECK_EQ_i32(0xFF, ecosim::kernel::neighbor_mask(g.cur(), 3, 3, 1, 1, ECOSIM_TOPOLOGY_BOUNDED));

  for (size_t i = 0; i < 9; ++i) g.cur().species[i] = 0;
  g.cur().species[g.index(1, 0)] = 1;  // top middle
  CHECK_EQ_i32(0x02, ecosim::kernel::neighbor_mask(g.cur(), 3, 3, 1, 1, ECOSIM_TOPOLOGY_BOUNDED));

  g.cur().species[g.index(0, 0)] = 1;  // top left
  CHECK_EQ_i32(0x03, ecosim::kernel::neighbor_mask(g.cur(), 3, 3, 1, 1, ECOSIM_TOPOLOGY_BOUNDED));

  g.cur().species[g.index(2, 2)] = 1;  // bottom right
  CHECK_EQ_i32(0x83, ecosim::kernel::neighbor_mask(g.cur(), 3, 3, 1, 1, ECOSIM_TOPOLOGY_BOUNDED));
}

ECOSIM_TEST(neighbor_mask_wraps_on_torus_but_not_when_bounded) {
  ecosim::Grid g;
  g.allocate(4, 4);
  g.cur().species[g.index(3, 3)] = 1;
  // On a torus the far corner (3,3) is the *top-left* neighbour of (0,0), i.e.
  // offset (-1,-1) = bit0. Bounded, it is simply out of reach.
  CHECK_EQ_i32(0x01, ecosim::kernel::neighbor_mask(g.cur(), 4, 4, 0, 0, ECOSIM_TOPOLOGY_TORUS));
  CHECK_EQ_i32(0x00, ecosim::kernel::neighbor_mask(g.cur(), 4, 4, 0, 0, ECOSIM_TOPOLOGY_BOUNDED));
}

ECOSIM_TEST(fast_matches_reference_on_random_256_seed_12345) {
  Pair pair{256, 256, {}, {}};
  seed_pair(pair, 12345, 300);
  run_both(pair, 20, ecosim::RuleTable::life(), ECOSIM_TOPOLOGY_TORUS);
  CHECK_TRUE(pair.a.cur().species == pair.b.cur().species);
  CHECK_TRUE(pair.a.cur().age == pair.b.cur().age);
}

ECOSIM_TEST(fast_matches_reference_bounded_and_highlife_64) {
  const uint64_t seeds[] = {7, 99991, 424242};
  for (const uint64_t seed : seeds) {
    Pair pair{64, 64, {}, {}};
    seed_pair(pair, seed, 450);
    run_both(pair, 30, ecosim::RuleTable::life(), ECOSIM_TOPOLOGY_BOUNDED);
    CHECK_TRUE(pair.a.cur().species == pair.b.cur().species);

    ecosim::RuleTable highlife;
    highlife.birth[3] = 1;
    highlife.birth[6] = 1;
    highlife.survive[2] = 1;
    highlife.survive[3] = 1;
    seed_pair(pair, seed, 450);
    run_both(pair, 30, highlife, ECOSIM_TOPOLOGY_TORUS);
    CHECK_TRUE(pair.a.cur().species == pair.b.cur().species);
  }
}

ECOSIM_TEST(fast_kernel_still_produces_the_known_blinker) {
  // The equivalence tests could in principle agree on a wrong answer; this one
  // pins the fast kernel against a hand-derived result as well.
  ecosim::Grid g;
  g.allocate(5, 5);
  g.cur().species[g.index(1, 2)] = 1;
  g.cur().species[g.index(2, 2)] = 1;
  g.cur().species[g.index(3, 2)] = 1;
  g.copy_cur_to_nxt();
  ecosim::kernel::step_fast(g.cur(), g.nxt(), 5, 5, ecosim::RuleTable::life(),
                            ECOSIM_TOPOLOGY_TORUS);
  g.swap_buffers();
  CHECK_EQ_i32(3, ecosim::kernel::count_alive(g.cur()));
  CHECK_EQ_i32(1, g.cur().species[g.index(2, 1)]);
  CHECK_EQ_i32(1, g.cur().species[g.index(2, 3)]);
  CHECK_EQ_i32(0, g.cur().species[g.index(1, 2)]);
}
