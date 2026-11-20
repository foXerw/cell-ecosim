#include "core/world.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>

#include "core/kernel_fast.h"
#include "util/error.h"

namespace ecosim {

bool World::validate(const EcosimConfig& cfg, char* why, int32_t cap) {
  // Prefix tolerance: a client built against an older header may pass a
  // smaller struct_size as long as it supplied everything we read first.
  if (cfg.struct_size < static_cast<int32_t>(offsetof(EcosimConfig, rng_seed))) {
    std::snprintf(why, cap, "EcosimConfig.struct_size = %d 太小（本构建需要 %d）",
                  cfg.struct_size, static_cast<int32_t>(sizeof(EcosimConfig)));
    return false;
  }
  if (cfg.w < 1 || cfg.h < 1) {
    std::snprintf(why, cap, "网格尺寸必须 >= 1x1，收到 %dx%d", cfg.w, cfg.h);
    return false;
  }
  if (cfg.w > ECOSIM_MAX_DIM || cfg.h > ECOSIM_MAX_DIM) {
    std::snprintf(why, cap, "网格尺寸超出上限 %d，收到 %dx%d", ECOSIM_MAX_DIM, cfg.w, cfg.h);
    return false;
  }
  if (cfg.mode != ECOSIM_MODE_BIRSURV && cfg.mode != ECOSIM_MODE_ECOSYSTEM) {
    std::snprintf(why, cap, "未知模式 %d", cfg.mode);
    return false;
  }
  if (cfg.mode == ECOSIM_MODE_ECOSYSTEM) {
    std::snprintf(why, cap, "生态模式在里程碑 M2 才可用，当前只支持 B/S 规则模式");
    return false;
  }
  if (cfg.topology != ECOSIM_TOPOLOGY_TORUS && cfg.topology != ECOSIM_TOPOLOGY_BOUNDED) {
    std::snprintf(why, cap, "未知拓扑 %d", cfg.topology);
    return false;
  }
  if (cfg.threads < 0) {
    std::snprintf(why, cap, "threads 不能为负，收到 %d（0 表示自动选择）", cfg.threads);
    return false;
  }
  return true;
}

World::World(const EcosimConfig& cfg) : cfg_(cfg), rules_(RuleTable::from_config(cfg)) {
  char why[256];
  if (!validate(cfg, why, static_cast<int32_t>(sizeof(why)))) {
    throw std::invalid_argument(why);
  }
  if (cfg_.struct_size < static_cast<int32_t>(sizeof(EcosimConfig))) {
    // Older client: fill the trailing fields we do have with defaults.
    cfg_.rng_seed = 0;
  }
  // 0 means "engine decides"; explicit counts are capped so a silly config
  // cannot spawn thousands of threads.
  cfg_.threads = cfg_.threads == 0 ? parallel::default_threads() : std::clamp(cfg_.threads, 1, 32);
  for (int i = 0; i < 9; ++i) {
    cfg_.birth_mask[i] = cfg_.birth_mask[i] ? 1 : 0;
    cfg_.survive_mask[i] = cfg_.survive_mask[i] ? 1 : 0;
  }
  grid_.allocate(cfg_.w, cfg_.h);
  pool_ = std::make_unique<parallel::Pool>(cfg_.threads);
}

int32_t World::step(int32_t n_steps) {
  if (n_steps < 0) {
    set_error("n_steps 不能为负");
    return ECOSIM_ERR_INVALID;
  }
  if (n_steps == 0) return ECOSIM_OK;

  const int32_t band_rows = parallel::band_rows_for(grid_.h(), pool_->size());
  for (int32_t i = 0; i < n_steps; ++i) {
    // Bands write disjoint row ranges of nxt and only read cur, so they can run
    // concurrently. The result is proven identical to a single-threaded pass by
    // tests/test_parallel.cpp.
    pool_->run_bands(grid_.h(), band_rows, [&](int32_t y0, int32_t y1) {
      kernel::step_fast(grid_.cur(), grid_.nxt(), grid_.w(), grid_.h(), y0, y1, rules_,
                        cfg_.topology);
    });
    grid_.swap_buffers();
  }
  generation_ += static_cast<uint64_t>(n_steps);
  return ECOSIM_OK;
}

int32_t World::paint(const EcosimPaintOp* ops, int32_t count) {
  if (ops == nullptr || count < 0) {
    set_error(count == 0 ? "paint 需要至少一个 op" : "ops 为 NULL 或 count 为负");
    return ECOSIM_ERR_INVALID;
  }

  // Validate everything first: a stroke is applied atomically or not at all.
  for (int32_t i = 0; i < count; ++i) {
    const EcosimPaintOp& op = ops[i];
    if (op.struct_size < static_cast<int32_t>(sizeof(EcosimPaintOp))) {
      set_error("EcosimPaintOp.struct_size 太小");
      return ECOSIM_ERR_SIZE;
    }
    if (op.species < 0 || op.species >= ECOSIM_MAX_SPECIES) {
      char msg[128];
      std::snprintf(msg, sizeof(msg), "species %d 不在 0..%d 范围内", op.species,
                    ECOSIM_MAX_SPECIES - 1);
      set_error(msg);
      return ECOSIM_ERR_INVALID;
    }
    if (op.x0 < 0 || op.y0 < 0 || op.x1 > grid_.w() || op.y1 > grid_.h() || op.x0 >= op.x1 ||
        op.y0 >= op.y1) {
      char msg[160];
      std::snprintf(msg, sizeof(msg),
                    "矩形 [%d,%d)x[%d,%d) 超出网格 %dx%d（区间为半开，需 x0<x1 且 y0<y1）", op.x0,
                    op.x1, op.y0, op.y1, grid_.w(), grid_.h());
      set_error(msg);
      return ECOSIM_ERR_RANGE;
    }
  }

  std::vector<uint8_t>& species = grid_.cur().species;
  for (int32_t i = 0; i < count; ++i) {
    const EcosimPaintOp& op = ops[i];
    for (int32_t y = op.y0; y < op.y1; ++y) {
      uint8_t* row = species.data() + static_cast<size_t>(y) * grid_.w();
      for (int32_t x = op.x0; x < op.x1; ++x) row[x] = static_cast<uint8_t>(op.species);
    }
  }

  // The scratch buffer must not hold a stale generation: without this, the next
  // step would read cur (fine) but a later swap could expose pre-paint data.
  grid_.copy_cur_to_nxt();
  return ECOSIM_OK;
}

int32_t World::render_frame(uint8_t* bgra, int32_t row_pitch, const EcosimPalette* palette) const {
  if (bgra == nullptr || palette == nullptr) {
    set_error("render_frame 需要非空缓冲与调色盘");
    return ECOSIM_ERR_INVALID;
  }
  if (palette->struct_size < static_cast<int32_t>(sizeof(EcosimPalette))) {
    set_error("EcosimPalette.struct_size 太小");
    return ECOSIM_ERR_SIZE;
  }
  const int32_t min_pitch = grid_.w() * 4;
  if (row_pitch < min_pitch) {
    char msg[128];
    std::snprintf(msg, sizeof(msg), "row_pitch %d 小于网格宽度所需 %d 字节", row_pitch, min_pitch);
    set_error(msg);
    return ECOSIM_ERR_INVALID;
  }

  const std::vector<uint8_t>& species = grid_.cur().species;
  for (int32_t y = 0; y < grid_.h(); ++y) {
    const uint8_t* row = species.data() + static_cast<size_t>(y) * grid_.w();
    uint8_t* out = bgra + static_cast<size_t>(y) * row_pitch;
    for (int32_t x = 0; x < grid_.w(); ++x) {
      const uint32_t colour = palette->cell_bgra[row[x] % ECOSIM_MAX_SPECIES];
      std::memcpy(out + static_cast<size_t>(x) * 4, &colour, sizeof(colour));
    }
  }
  return ECOSIM_OK;
}

int32_t World::set_topology(int32_t topology) {
  if (topology != ECOSIM_TOPOLOGY_TORUS && topology != ECOSIM_TOPOLOGY_BOUNDED) {
    set_error("未知拓扑");
    return ECOSIM_ERR_INVALID;
  }
  cfg_.topology = topology;
  return ECOSIM_OK;
}

}  // namespace ecosim
