#include "core/world.h"

#include <cstddef>
#include <cstdio>
#include <stdexcept>

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

World::World(const EcosimConfig& cfg) : cfg_(cfg) {
  char why[256];
  if (!validate(cfg, why, static_cast<int32_t>(sizeof(why)))) {
    throw std::invalid_argument(why);
  }
  if (cfg_.struct_size < static_cast<int32_t>(sizeof(EcosimConfig))) {
    // Older client: fill the trailing fields we do have with defaults.
    cfg_.rng_seed = 0;
  }
  cfg_.threads = cfg_.threads == 0 ? 1 : cfg_.threads;
  for (int i = 0; i < 9; ++i) {
    cfg_.birth_mask[i] = cfg_.birth_mask[i] ? 1 : 0;
    cfg_.survive_mask[i] = cfg_.survive_mask[i] ? 1 : 0;
  }
  grid_.allocate(cfg_.w, cfg_.h);
}

int32_t World::step(int32_t n_steps) {
  if (n_steps < 0) {
    set_error("n_steps 不能为负");
    return ECOSIM_ERR_INVALID;
  }
  if (n_steps == 0) return ECOSIM_OK;

  for (int32_t i = 0; i < n_steps; ++i) {
    // Milestone M1 task 4 replaces this with kernel::step_reference().
    grid_.copy_cur_to_nxt();
    grid_.swap_buffers();
  }
  generation_ += static_cast<uint64_t>(n_steps);
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
