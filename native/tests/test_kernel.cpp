#include "check.h"

#include <cstring>
#include <set>
#include <utility>
#include <vector>

#include "ecosim_abi.h"

namespace {

using Cells = std::set<std::pair<int32_t, int32_t>>;

struct World {
  EcosimConfig cfg{};
  EcosimWorld* h = nullptr;

  explicit World(int32_t w, int32_t h_, int32_t topology = ECOSIM_TOPOLOGY_TORUS) {
    cfg.struct_size = sizeof(EcosimConfig);
    cfg.w = w;
    cfg.h = h_;
    cfg.mode = ECOSIM_MODE_BIRSURV;
    cfg.topology = topology;
    cfg.threads = 1;
    cfg.birth_mask[3] = 1;
    cfg.survive_mask[2] = 1;
    cfg.survive_mask[3] = 1;
    cfg.rng_seed = 1;
    h = ecosim_world_create(&cfg);
  }

  ~World() { ecosim_world_destroy(h); }

  World(const World&) = delete;
  World& operator=(const World&) = delete;

  void use_custom_masks(const uint8_t* birth, const uint8_t* survive) {
    std::memcpy(cfg.birth_mask, birth, 9);
    std::memcpy(cfg.survive_mask, survive, 9);
    ecosim_world_destroy(h);
    h = ecosim_world_create(&cfg);
  }

  void paint(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t species) {
    EcosimPaintOp op{};
    op.struct_size = sizeof(EcosimPaintOp);
    op.x0 = x0;
    op.y0 = y0;
    op.x1 = x1;
    op.y1 = y1;
    op.species = species;
    CHECK_EQ_i32(ECOSIM_OK, ecosim_paint(h, &op, 1));
  }

  void paint_cells(const Cells& cells) {
    for (const auto& c : cells) paint(c.first, c.second, c.first + 1, c.second + 1, 1);
  }

  int32_t step(int32_t n = 1) { return ecosim_world_step(h, n); }

  Cells alive() const {
    Cells out;
    EcosimGridView view{};
    view.struct_size = sizeof(EcosimGridView);
    if (ecosim_world_grid_view(h, &view) != ECOSIM_OK) return out;
    for (int32_t y = 0; y < view.h; ++y) {
      for (int32_t x = 0; x < view.w; ++x) {
        if (view.species[static_cast<size_t>(y) * view.row_pitch_cells + x] != 0) {
          out.insert({x, y});
        }
      }
    }
    return out;
  }

  uint16_t age_at(int32_t x, int32_t y) const {
    EcosimGridView view{};
    view.struct_size = sizeof(EcosimGridView);
    if (ecosim_world_grid_view(h, &view) != ECOSIM_OK) return 0xFFFF;
    return view.age[static_cast<size_t>(y) * view.row_pitch_cells + x];
  }
};

#define CHECK_CELLS(expected, actual)                       \
  do {                                                      \
    const Cells e_ = (expected);                            \
    const Cells a_ = (actual);                              \
    if (e_ != a_) {                                         \
      std::string d = std::string(#actual) + " = {";        \
      for (const auto& c : a_) d += "(" + std::to_string(c.first) + "," + std::to_string(c.second) + ")"; \
      d += "} want {";                                      \
      for (const auto& c : e_) d += "(" + std::to_string(c.first) + "," + std::to_string(c.second) + ")"; \
      d += "}";                                             \
      ::ecosim::test::report(__FILE__, __LINE__, d);        \
    }                                                       \
  } while (0)

}  // namespace

// All expectations below are derived by hand from B3/S23, not recorded from a run.

ECOSIM_TEST(block_is_still_life) {
  World w(8, 8);
  w.paint(1, 1, 3, 3, 1);  // 2x2 at (1,1),(2,1),(1,2),(2,2)
  const Cells before = w.alive();
  CHECK_EQ_i32(4, static_cast<int32_t>(before.size()));
  CHECK_EQ_i32(ECOSIM_OK, w.step(1));
  CHECK_CELLS(before, w.alive());
  w.step(4);
  CHECK_CELLS(before, w.alive());
}

ECOSIM_TEST(blinker_period_two) {
  World w(5, 5);
  w.paint_cells({{1, 2}, {2, 2}, {3, 2}});
  // Middle cell has 2 live neighbours and survives; the two ends have 1 and die;
  // (2,1) and (2,3) each see exactly 3 and are born.
  w.step(1);
  CHECK_CELLS(Cells({{2, 1}, {2, 2}, {2, 3}}), w.alive());
  w.step(1);
  CHECK_CELLS(Cells({{1, 2}, {2, 2}, {3, 2}}), w.alive());
}

ECOSIM_TEST(glider_shifts_one_diag_over_four_steps) {
  // .O.      (1,0)
  // ..O  ->  (2,1)
  // OOO      (0,2),(1,2),(2,2)
  World w(16, 16);
  const Cells start{{1, 0}, {2, 1}, {0, 2}, {1, 2}, {2, 2}};
  w.paint_cells(start);
  Cells want;
  for (const auto& c : start) want.insert({c.first + 1, c.second + 1});
  CHECK_EQ_i32(ECOSIM_OK, w.step(4));
  CHECK_CELLS(want, w.alive());
  CHECK_EQ_i32(5, static_cast<int32_t>(w.alive().size()));
}

ECOSIM_TEST(bounded_edge_blinker_becomes_pair_then_extinct) {
  // Bounded 5x5 with (0,0),(1,0),(2,0): the ends see only 1 neighbour and die,
  // the middle sees 2 and survives, and (1,1) sees all three so it is born.
  World w(5, 5, ECOSIM_TOPOLOGY_BOUNDED);
  w.paint_cells({{0, 0}, {1, 0}, {2, 0}});
  w.step(1);
  CHECK_CELLS(Cells({{1, 0}, {1, 1}}), w.alive());
  w.step(1);  // each of the pair now sees exactly 1 neighbour
  CHECK_CELLS(Cells{}, w.alive());
}

ECOSIM_TEST(torus_wraps_vertical_blinker_across_top_edge) {
  // Same seed on a torus: (1,4) is a neighbour of the whole row y=0, so it sees
  // 3 and is born too -- the blinker flips vertical *through* the wrap.
  World w(5, 5, ECOSIM_TOPOLOGY_TORUS);
  w.paint_cells({{0, 0}, {1, 0}, {2, 0}});
  w.step(1);
  CHECK_CELLS(Cells({{1, 4}, {1, 0}, {1, 1}}), w.alive());
}

ECOSIM_TEST(custom_birth_mask_one_expands_to_ring) {
  // B1/S0: every dead cell touching the single live cell sees exactly 1
  // neighbour and is born; the original cell sees 0 and dies.
  uint8_t birth[9] = {0, 1, 0, 0, 0, 0, 0, 0, 0};
  uint8_t survive[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
  World w(5, 5, ECOSIM_TOPOLOGY_BOUNDED);
  w.use_custom_masks(birth, survive);
  w.paint(2, 2, 3, 3, 1);
  w.step(1);
  CHECK_CELLS(Cells({{1, 1}, {2, 1}, {3, 1}, {1, 2}, {3, 2}, {1, 3}, {2, 3}, {3, 3}}), w.alive());
}

ECOSIM_TEST(ages_increment_for_surviving_cells) {
  World w(8, 8);
  w.paint(3, 3, 5, 5, 1);
  CHECK_EQ_i32(0, w.age_at(3, 3));
  w.step(1);
  CHECK_EQ_i32(1, w.age_at(3, 3));
  w.step(2);
  CHECK_EQ_i32(3, w.age_at(3, 3));
}

ECOSIM_TEST(painted_cells_are_stepped_not_overwritten) {
  // Review Focus #2: painting while paused must survive into the next step.
  World w(9, 9);
  w.paint_cells({{3, 4}, {4, 4}, {5, 4}});
  w.step(1);
  CHECK_CELLS(Cells({{4, 3}, {4, 4}, {4, 5}}), w.alive());
}

ECOSIM_TEST(paint_rejects_out_of_range_rect) {
  World w(4, 4);
  EcosimPaintOp op{};
  op.struct_size = sizeof(EcosimPaintOp);
  op.x0 = 2;
  op.y0 = 2;
  op.x1 = 5;  // past the edge
  op.y1 = 3;
  op.species = 1;
  CHECK_EQ_i32(ECOSIM_ERR_RANGE, ecosim_paint(w.h, &op, 1));
  CHECK_TRUE(std::strlen(ecosim_last_error()) > 0);
  op.x1 = 4;
  op.species = ECOSIM_MAX_SPECIES;  // not a slot in the palette
  CHECK_EQ_i32(ECOSIM_ERR_INVALID, ecosim_paint(w.h, &op, 1));
}

ECOSIM_TEST(set_topology_takes_effect_immediately) {
  // Design appendix A.1: topology is hot, state is not lost. Start on a torus
  // with the wrapped vertical blinker {(1,4),(1,0),(1,1)}, then cut the wrap:
  // (1,4) stops being a neighbour of (1,0), every cell drops to 1 neighbour.
  World w(5, 5, ECOSIM_TOPOLOGY_TORUS);
  w.paint_cells({{0, 0}, {1, 0}, {2, 0}});
  w.step(1);
  CHECK_EQ_i32(3, static_cast<int32_t>(w.alive().size()));
  CHECK_EQ_i32(ECOSIM_OK, ecosim_set_topology(w.h, ECOSIM_TOPOLOGY_BOUNDED));
  w.step(1);
  CHECK_CELLS(Cells{}, w.alive());
  CHECK_EQ_i32(ECOSIM_ERR_INVALID, ecosim_set_topology(w.h, 7));
}

ECOSIM_TEST(alive_count_matches_population_after_steps) {
  World w(16, 16);
  w.paint_cells({{1, 1}, {2, 1}, {3, 1}});
  CHECK_EQ_i32(3, static_cast<int32_t>(w.alive().size()));
  w.step(1);
  CHECK_EQ_i32(3, static_cast<int32_t>(w.alive().size()));
  w.paint(0, 0, 2, 2, 1);  // a block overlapping the blinker's row
  CHECK_EQ_i32(7, static_cast<int32_t>(w.alive().size()));
}
