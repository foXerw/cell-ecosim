#include "core/world.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>

#include "core/kernel_fast.h"
#include "core/render.h"
#include "rules/pipeline.h"
#include "core/species.h"
#include "util/error.h"
#include "util/rng.h"

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
  params_ = default_params();
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
    if (cfg_.mode == ECOSIM_MODE_ECOSYSTEM) {
      // The pipeline bands its own work internally (candidate lists, then a
      // serial apply in cell order) - see rules/ecosystem.cpp.
      run_ecosystem(*this);
    } else {
      // Bands write disjoint row ranges of nxt and only read cur, so they can
      // run concurrently. The result is proven identical to a single-threaded
      // pass by tests/test_parallel.cpp.
      pool_->run_bands(grid_.h(), band_rows, [&](int32_t y0, int32_t y1) {
        kernel::step_fast(grid_.cur(), grid_.nxt(), grid_.w(), grid_.h(), y0, y1, rules_,
                          cfg_.topology);
      });
      grid_.swap_buffers();
    }
    generation_ += 1;
  }
  return ECOSIM_OK;
}

int32_t World::set_params(const EcosimParams& p) {
  char why[256];
  if (!validate_params(p, why, static_cast<int32_t>(sizeof(why)))) {
    set_error(why);
    return ECOSIM_ERR_INVALID;
  }
  params_ = p;
  return ECOSIM_OK;
}

int32_t World::get_param(int32_t index, int32_t* out_value) const {
  if (out_value == nullptr) {
    set_error("out_value 为 NULL");
    return ECOSIM_ERR_INVALID;
  }
  if (param_desc(index) == nullptr) {
    char msg[96];
    std::snprintf(msg, sizeof(msg), "参数索引 %d 不存在（共 %d 个）", index, param_count());
    set_error(msg);
    return ECOSIM_ERR_RANGE;
  }
  *out_value = param_get(params_, index);
  return ECOSIM_OK;
}

int32_t World::set_param(int32_t index, int32_t value) {
  EcosimParams next = params_;
  if (!param_set(next, index, value)) {
    const ParamDesc* d = param_desc(index);
    char msg[160];
    if (d == nullptr) {
      std::snprintf(msg, sizeof(msg), "参数索引 %d 不存在", index);
    } else {
      std::snprintf(msg, sizeof(msg), "参数「%s」不接受值 %d（允许 %d..%d）", d->name, value,
                    d->min_value, d->max_value);
    }
    set_error(msg);
    return ECOSIM_ERR_INVALID;
  }
  params_ = next;
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
  return render::species_to_bgra(grid_.cur().species.data(), grid_.w(), grid_.h(), bgra,
                                 row_pitch, palette);
}

int32_t World::load_species(const uint8_t* species, int32_t count) {
  if (species == nullptr || count <= 0) {
    set_error("load_species 需要非空平面与正数长度");
    return ECOSIM_ERR_INVALID;
  }
  if (static_cast<size_t>(count) != grid_.count()) {
    char msg[128];
    std::snprintf(msg, sizeof(msg), "平面长度 %d 与网格 %dx%d 不符", count, grid_.w(),
                  grid_.h());
    set_error(msg);
    return ECOSIM_ERR_RANGE;
  }
  Planes& cur = grid_.cur();
  for (size_t i = 0; i < cur.species.size(); ++i) {
    const uint8_t s = species[i] % ECOSIM_MAX_SPECIES;
    cur.species[i] = s;
    cur.age[i] = 0;
    cur.energy[i] = s ? species_of(s).initial_energy : 0;
  }
  grid_.copy_cur_to_nxt();
  return ECOSIM_OK;
}

int32_t World::clear(int32_t fill_species) {
  if (fill_species < 0 || fill_species >= ECOSIM_MAX_SPECIES) {
    char msg[96];
    std::snprintf(msg, sizeof(msg), "species %d 不在 0..%d 范围内", fill_species,
                  ECOSIM_MAX_SPECIES - 1);
    set_error(msg);
    return ECOSIM_ERR_INVALID;
  }
  Planes& cur = grid_.cur();
  const uint16_t energy = species_of(fill_species).initial_energy;
  std::fill(cur.species.begin(), cur.species.end(), static_cast<uint8_t>(fill_species));
  std::fill(cur.age.begin(), cur.age.end(), uint16_t{0});
  std::fill(cur.energy.begin(), cur.energy.end(), energy);
  std::fill(cur.resource.begin(), cur.resource.end(), uint8_t{0});
  grid_.copy_cur_to_nxt();
  return ECOSIM_OK;
}

int32_t World::randomize(const EcosimSeedConfig& seed_cfg) {
  if (seed_cfg.struct_size < static_cast<int32_t>(sizeof(EcosimSeedConfig))) {
    set_error("EcosimSeedConfig.struct_size 太小");
    return ECOSIM_ERR_SIZE;
  }
  if (seed_cfg.species < 0 || seed_cfg.species >= ECOSIM_MAX_SPECIES) {
    char msg[96];
    std::snprintf(msg, sizeof(msg), "species %d 不在 0..%d 范围内", seed_cfg.species,
                  ECOSIM_MAX_SPECIES - 1);
    set_error(msg);
    return ECOSIM_ERR_INVALID;
  }
  if (seed_cfg.density_permille > 1000) {
    char msg[96];
    std::snprintf(msg, sizeof(msg), "density %u 超出 0..1000", seed_cfg.density_permille);
    set_error(msg);
    return ECOSIM_ERR_INVALID;
  }

  const uint64_t seed = seed_cfg.seed != 0 ? seed_cfg.seed : cfg_.rng_seed;
  const uint16_t energy = species_of(seed_cfg.species).initial_energy;
  Planes& cur = grid_.cur();
  const size_t n = grid_.count();
  for (size_t i = 0; i < n; ++i) {
    const bool alive = (rng::draw(seed, generation_, i) % 1000ull) < seed_cfg.density_permille;
    cur.species[i] = alive ? static_cast<uint8_t>(seed_cfg.species) : 0;
    cur.age[i] = 0;
    cur.energy[i] = alive ? energy : 0;
    cur.resource[i] = 0;
  }
  grid_.copy_cur_to_nxt();
  return ECOSIM_OK;
}

int32_t World::stats(EcosimStats& out) const {
  if (out.struct_size < static_cast<int32_t>(sizeof(EcosimStats))) {
    set_error("EcosimStats.struct_size 太小");
    return ECOSIM_ERR_SIZE;
  }
  const Planes& cur = grid_.cur();
  out.generation = generation_;
  out.w = grid_.w();
  out.h = grid_.h();
  std::memset(out.species_count, 0, sizeof(out.species_count));
  int32_t alive = 0;
  for (const uint8_t s : cur.species) {
    if (s == 0) continue;
    ++alive;
    ++out.species_count[s % ECOSIM_MAX_SPECIES];
  }
  out.alive_count = alive;
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
