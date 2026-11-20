#include "check.h"

#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "ecosim_abi.h"

#ifndef ECOSIM_DATA_DIR
#error "ECOSIM_DATA_DIR must point at data/patterns"
#endif

namespace {

using Cells = std::set<std::pair<int32_t, int32_t>>;

std::string read_file(const std::string& name) {
  std::ifstream in(ECOSIM_DATA_DIR "/" + name, std::ios::binary);
  if (!in) return {};
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

/// A pattern loaded from data/patterns, placed away from the corner so that
/// bounded grids do not eat half of it.
struct Pattern {
  EcosimWorld* w = nullptr;

  Pattern(const std::string& file, int32_t gw, int32_t gh, int32_t origin_x, int32_t origin_y,
          int32_t topology = ECOSIM_TOPOLOGY_BOUNDED) {
    const std::string text = read_file(file);
    CHECK_TRUE(!text.empty());
    EcosimConfig cfg{};
    cfg.struct_size = sizeof(EcosimConfig);
    cfg.w = gw;
    cfg.h = gh;
    cfg.mode = ECOSIM_MODE_BIRSURV;
    cfg.topology = topology;
    cfg.threads = 0;  // auto: the parallel path is proven identical to serial
    cfg.birth_mask[3] = 1;
    cfg.survive_mask[2] = 1;
    cfg.survive_mask[3] = 1;
    w = ecosim_world_create(&cfg);
    CHECK_TRUE(w != nullptr);
    if (!w) return;
    CHECK_EQ_i32(ECOSIM_OK, ecosim_from_rle_at(w, text.c_str(), origin_x, origin_y));
  }

  ~Pattern() { ecosim_world_destroy(w); }

  Pattern(const Pattern&) = delete;
  Pattern& operator=(const Pattern&) = delete;

  Cells cells() const {
    Cells out;
    EcosimGridView view{};
    view.struct_size = sizeof(EcosimGridView);
    CHECK_EQ_i32(ECOSIM_OK, ecosim_world_grid_view(w, &view));
    for (int32_t y = 0; y < view.h; ++y) {
      for (int32_t x = 0; x < view.w; ++x) {
        if (view.species[static_cast<size_t>(y) * view.row_pitch_cells + x] != 0) {
          out.insert({x, y});
        }
      }
    }
    return out;
  }

  void step(int32_t n) { CHECK_EQ_i32(ECOSIM_OK, ecosim_world_step(w, n)); }
};

Cells shift(const Cells& in, int32_t dx, int32_t dy, int32_t w, int32_t h) {
  Cells out;
  for (const auto& c : in) {
    out.insert({(c.first + dx + w) % w, (c.second + dy + h) % h});
  }
  return out;
}

}  // namespace

// A file earns its place in data/patterns by behaving like the object it claims
// to be. Texts copied from memory are not trusted: the Gosper gun, the pulsar
// and the LWSS all failed these checks and were dropped until they come from a
// source that can be verified (docs/m1-checklist.md).

ECOSIM_TEST(pattern_block_is_a_still_life) {
  Pattern p("block.rle", 12, 12, 4, 4, ECOSIM_TOPOLOGY_TORUS);
  const Cells start = p.cells();
  CHECK_EQ_i32(4, static_cast<int32_t>(start.size()));
  p.step(10);
  CHECK_TRUE(p.cells() == start);
}

ECOSIM_TEST(pattern_beehive_is_a_still_life) {
  Pattern p("beehive.rle", 12, 12, 4, 4, ECOSIM_TOPOLOGY_TORUS);
  const Cells start = p.cells();
  CHECK_EQ_i32(6, static_cast<int32_t>(start.size()));
  p.step(10);
  CHECK_TRUE(p.cells() == start);
}

ECOSIM_TEST(pattern_blinker_has_period_two) {
  Pattern p("blinker.rle", 12, 12, 4, 4, ECOSIM_TOPOLOGY_TORUS);
  const Cells start = p.cells();
  CHECK_EQ_i32(3, static_cast<int32_t>(start.size()));
  p.step(1);
  CHECK_TRUE(p.cells() != start);
  p.step(1);
  CHECK_TRUE(p.cells() == start);
}

ECOSIM_TEST(pattern_glider_translates_one_cell_diagonally_per_four_generations) {
  Pattern p("glider.rle", 20, 20, 4, 4, ECOSIM_TOPOLOGY_TORUS);
  const Cells start = p.cells();
  CHECK_EQ_i32(5, static_cast<int32_t>(start.size()));
  p.step(4);
  const Cells after = p.cells();
  bool moved = false;
  for (int32_t dx : {-1, 1}) {
    for (int32_t dy : {-1, 1}) {
      if (after == shift(start, dx, dy, 20, 20)) moved = true;
    }
  }
  CHECK_TRUE(moved);
  p.step(16);  // and it keeps flying rather than dissolving
  CHECK_EQ_i32(5, static_cast<int32_t>(p.cells().size()));
}

ECOSIM_TEST(pattern_r_pentomino_is_a_methuselah_that_stabilises) {
  // Bounded and well inside the grid: its ejecta fly outward, leave the board
  // and stop mattering, which is what makes the ash stable to look at.
  Pattern p("r_pentomino.rle", 200, 200, 100, 100);
  CHECK_EQ_i32(5, static_cast<int32_t>(p.cells().size()));
  p.step(300);
  CHECK_TRUE(p.cells().size() > 50);  // a methuselah, not a flicker
  p.step(1100);
  const Cells late = p.cells();
  // 24 generations apart: divisible by every oscillator period the ash can
  // contain (2, 3, 4, 6, 8), and by then the escaping gliders have left the
  // board and died, so a settled world reads back identical.
  p.step(24);
  CHECK_TRUE(p.cells() == late);
}
