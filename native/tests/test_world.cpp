#include "check.h"

#include <cstdlib>
#include <cstring>

#include "ecosim_abi.h"

namespace {

EcosimConfig make_config(int32_t w, int32_t h) {
  EcosimConfig cfg{};
  cfg.struct_size = sizeof(EcosimConfig);
  cfg.w = w;
  cfg.h = h;
  cfg.mode = ECOSIM_MODE_BIRSURV;
  cfg.topology = ECOSIM_TOPOLOGY_TORUS;
  cfg.threads = 1;
  cfg.birth_mask[3] = 1;
  cfg.survive_mask[2] = 1;
  cfg.survive_mask[3] = 1;
  cfg.rng_seed = 1;
  return cfg;
}

}  // namespace

ECOSIM_TEST(abi_version_is_one) { CHECK_EQ_i32(1, ecosim_abi_version()); }

ECOSIM_TEST(abi_struct_size_agrees_with_compiled_layout) {
  CHECK_EQ_i32((int32_t)sizeof(EcosimConfig), ecosim_abi_struct_size(ECOSIM_STRUCT_CONFIG));
  CHECK_EQ_i32((int32_t)sizeof(EcosimGridView), ecosim_abi_struct_size(ECOSIM_STRUCT_GRID_VIEW));
  CHECK_EQ_i32((int32_t)sizeof(EcosimPalette), ecosim_abi_struct_size(ECOSIM_STRUCT_PALETTE));
  CHECK_EQ_i32((int32_t)sizeof(EcosimPaintOp), ecosim_abi_struct_size(ECOSIM_STRUCT_PAINT_OP));
  CHECK_EQ_i32((int32_t)sizeof(EcosimStats), ecosim_abi_struct_size(ECOSIM_STRUCT_STATS));
  CHECK_EQ_i32((int32_t)sizeof(EcosimSeedConfig), ecosim_abi_struct_size(ECOSIM_STRUCT_SEED));
}

ECOSIM_TEST(world_create_step_destroy) {
  EcosimConfig cfg = make_config(8, 8);
  EcosimWorld* w = ecosim_world_create(&cfg);
  CHECK_TRUE(w != nullptr);
  if (!w) return;

  CHECK_EQ_i32(ECOSIM_OK, ecosim_world_step(w, 3));
  CHECK_EQ_i64(3, (int64_t)ecosim_generation(w));

  EcosimGridView view{};
  view.struct_size = sizeof(EcosimGridView);
  CHECK_EQ_i32(ECOSIM_OK, ecosim_world_grid_view(w, &view));
  CHECK_EQ_i32(8, view.w);
  CHECK_EQ_i32(8, view.h);
  CHECK_EQ_i32(8, view.row_pitch_cells);
  CHECK_TRUE(view.species != nullptr);
  CHECK_TRUE(view.resource != nullptr);
  CHECK_TRUE(view.energy != nullptr);
  CHECK_TRUE(view.age != nullptr);

  ecosim_world_destroy(w);
}

ECOSIM_TEST(world_step_zero_is_noop_and_negative_rejected) {
  EcosimConfig cfg = make_config(4, 4);
  EcosimWorld* w = ecosim_world_create(&cfg);
  CHECK_TRUE(w != nullptr);
  if (!w) return;
  CHECK_EQ_i32(ECOSIM_OK, ecosim_world_step(w, 0));
  CHECK_EQ_i64(0, (int64_t)ecosim_generation(w));
  CHECK_EQ_i32(ECOSIM_ERR_INVALID, ecosim_world_step(w, -1));
  CHECK_EQ_i32(ECOSIM_ERR_INVALID, ecosim_world_step(nullptr, 1));
  ecosim_world_destroy(w);
}

ECOSIM_TEST(grid_planes_are_zero_initialised) {
  EcosimConfig cfg = make_config(5, 3);
  EcosimWorld* w = ecosim_world_create(&cfg);
  CHECK_TRUE(w != nullptr);
  if (!w) return;
  EcosimGridView view{};
  view.struct_size = sizeof(EcosimGridView);
  CHECK_EQ_i32(ECOSIM_OK, ecosim_world_grid_view(w, &view));
  for (int32_t i = 0; i < 15; ++i) CHECK_EQ_i32(0, view.species[i]);
  ecosim_world_destroy(w);
}

ECOSIM_TEST(world_create_rejects_bad_dims_and_struct_size) {
  EcosimConfig cfg = make_config(0, 8);
  CHECK_TRUE(ecosim_world_create(&cfg) == nullptr);
  CHECK_TRUE(std::strlen(ecosim_last_error()) > 0);

  cfg = make_config(ECOSIM_MAX_DIM + 1, 8);
  CHECK_TRUE(ecosim_world_create(&cfg) == nullptr);

  cfg = make_config(8, ECOSIM_MAX_DIM + 1);
  CHECK_TRUE(ecosim_world_create(&cfg) == nullptr);

  cfg = make_config(8, 8);
  cfg.struct_size = 4;
  CHECK_TRUE(ecosim_world_create(&cfg) == nullptr);
  CHECK_TRUE(std::strlen(ecosim_last_error()) > 0);

  CHECK_TRUE(ecosim_world_create(nullptr) == nullptr);
}

ECOSIM_TEST(grid_view_rejects_undersized_struct) {
  EcosimConfig cfg = make_config(8, 8);
  EcosimWorld* w = ecosim_world_create(&cfg);
  CHECK_TRUE(w != nullptr);
  if (!w) return;
  EcosimGridView view{};
  view.struct_size = 8;  // smaller than the compiled struct: caller from an older ABI
  CHECK_EQ_i32(ECOSIM_ERR_SIZE, ecosim_world_grid_view(w, &view));
  CHECK_EQ_i32(ECOSIM_ERR_INVALID, ecosim_world_grid_view(w, nullptr));
  ecosim_world_destroy(w);
}

ECOSIM_TEST(not_yet_implemented_exports_report_state_error_not_crash) {
  EcosimConfig cfg = make_config(4, 4);
  EcosimWorld* w = ecosim_world_create(&cfg);
  CHECK_TRUE(w != nullptr);
  if (!w) return;

  EcosimStats stats{};
  stats.struct_size = sizeof(EcosimStats);
  CHECK_EQ_i32(ECOSIM_ERR_STATE, ecosim_stats(w, &stats));
  CHECK_EQ_i32(ECOSIM_ERR_STATE, ecosim_clear(w, 0));
  CHECK_TRUE(std::strlen(ecosim_last_error()) > 0);
  ecosim_world_destroy(w);
}
