// Thin extern "C" shell over ecosim::World. Nothing here owns state: it
// validates struct_size, translates C++ exceptions into return codes plus a
// thread-local diagnostic, and delegates.
#include "ecosim_abi.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#include "core/rle.h"
#include "core/world.h"
#include "util/error.h"

// The layout numbers below are what the managed side asserts against
// (app/Ecosim.Tests/AbiLayoutTests.cs). Changing one without the other breaks
// the build here, loudly, instead of corrupting memory at runtime.
static_assert(sizeof(EcosimConfig) == 52, "EcosimConfig layout drift");
static_assert(offsetof(EcosimConfig, threads) == 20, "threads offset drift");
static_assert(offsetof(EcosimConfig, birth_mask) == 24, "birth_mask offset drift");
static_assert(offsetof(EcosimConfig, survive_mask) == 33, "survive_mask offset drift");
static_assert(offsetof(EcosimConfig, rng_seed) == 44, "rng_seed offset drift");
static_assert(sizeof(EcosimGridView) == 48, "EcosimGridView layout drift");
static_assert(offsetof(EcosimGridView, species) == 16, "species pointer drift");
static_assert(sizeof(EcosimPalette) == 68, "EcosimPalette layout drift");
static_assert(sizeof(EcosimPaintOp) == 24, "EcosimPaintOp layout drift");
static_assert(sizeof(EcosimStats) == 88, "EcosimStats layout drift");
static_assert(offsetof(EcosimStats, generation) == 4, "generation offset drift");
static_assert(offsetof(EcosimStats, species_count) == 24, "species_count offset drift");
static_assert(sizeof(EcosimSeedConfig) == 20, "EcosimSeedConfig layout drift");

namespace {

// Exports that land in a later milestone keep the ABI surface complete so the
// managed side can compile against all of it today.
int32_t not_implemented(const char* fn) {
  char msg[128];
  std::snprintf(msg, sizeof(msg), "%s 尚未实现（属于后续里程碑）", fn);
  ecosim::set_error(msg);
  return ECOSIM_ERR_STATE;
}

}  // namespace

extern "C" {

int32_t ecosim_abi_version(void) { return ECOSIM_ABI_VERSION; }

int32_t ecosim_abi_struct_size(int32_t which) {
  switch (which) {
    case ECOSIM_STRUCT_CONFIG: return static_cast<int32_t>(sizeof(EcosimConfig));
    case ECOSIM_STRUCT_GRID_VIEW: return static_cast<int32_t>(sizeof(EcosimGridView));
    case ECOSIM_STRUCT_PALETTE: return static_cast<int32_t>(sizeof(EcosimPalette));
    case ECOSIM_STRUCT_PAINT_OP: return static_cast<int32_t>(sizeof(EcosimPaintOp));
    case ECOSIM_STRUCT_STATS: return static_cast<int32_t>(sizeof(EcosimStats));
    case ECOSIM_STRUCT_SEED: return static_cast<int32_t>(sizeof(EcosimSeedConfig));
    default: return 0;
  }
}

const char* ecosim_last_error(void) {
  const std::string& msg = ecosim::current_error();
  return msg.empty() ? "" : msg.c_str();
}

EcosimWorld* ecosim_world_create(const EcosimConfig* cfg) {
  ecosim::clear_error();
  if (cfg == nullptr) {
    ecosim::set_error("cfg 为 NULL");
    return nullptr;
  }
  char why[256];
  if (!ecosim::World::validate(*cfg, why, static_cast<int32_t>(sizeof(why)))) {
    ecosim::set_error(why);
    return nullptr;
  }
  try {
    return reinterpret_cast<EcosimWorld*>(new ecosim::World(*cfg));
  } catch (const std::exception& e) {
    ecosim::set_error(e.what());
    return nullptr;
  } catch (...) {
    ecosim::set_error("创建世界时发生未知错误");
    return nullptr;
  }
}

void ecosim_world_destroy(EcosimWorld* world) {
  // Destroying nullptr must stay harmless, matching C free() semantics.
  delete reinterpret_cast<ecosim::World*>(world);
}

static ecosim::World* as_world(EcosimWorld* world) {
  if (world == nullptr) {
    ecosim::set_error("world 句柄为 NULL");
    return nullptr;
  }
  return reinterpret_cast<ecosim::World*>(world);
}

int32_t ecosim_world_step(EcosimWorld* world, int32_t n_steps) {
  ecosim::clear_error();
  ecosim::World* w = as_world(world);
  if (!w) return ECOSIM_ERR_INVALID;
  return w->step(n_steps);
}

int32_t ecosim_set_topology(EcosimWorld* world, int32_t topology) {
  ecosim::clear_error();
  ecosim::World* w = as_world(world);
  if (!w) return ECOSIM_ERR_INVALID;
  return w->set_topology(topology);
}

int64_t ecosim_generation(const EcosimWorld* world) {
  ecosim::World* w = as_world(const_cast<EcosimWorld*>(world));
  return w ? static_cast<int64_t>(w->generation()) : -1;
}

int32_t ecosim_world_grid_view(const EcosimWorld* world, EcosimGridView* out) {
  ecosim::clear_error();
  ecosim::World* w = as_world(const_cast<EcosimWorld*>(world));
  if (!w) return ECOSIM_ERR_INVALID;
  if (out == nullptr) {
    ecosim::set_error("out 为 NULL");
    return ECOSIM_ERR_INVALID;
  }
  if (out->struct_size < static_cast<int32_t>(sizeof(EcosimGridView))) {
    ecosim::set_error("EcosimGridView.struct_size 太小：调用方比本构建更旧");
    return ECOSIM_ERR_SIZE;
  }
  const ecosim::Planes& p = w->grid().cur();
  out->w = w->grid().w();
  out->h = w->grid().h();
  out->row_pitch_cells = w->grid().w();
  out->species = p.species.data();
  out->resource = p.resource.data();
  out->energy = p.energy.data();
  out->age = p.age.data();
  return ECOSIM_OK;
}

int32_t ecosim_render_frame(const EcosimWorld*, uint8_t*, int32_t, const EcosimPalette*) {
  return not_implemented("ecosim_render_frame");
}

int32_t ecosim_paint(EcosimWorld* world, const EcosimPaintOp* ops, int32_t count) {
  ecosim::clear_error();
  ecosim::World* w = as_world(world);
  if (!w) return ECOSIM_ERR_INVALID;
  return w->paint(ops, count);
}

int32_t ecosim_clear(EcosimWorld*, int32_t) { return not_implemented("ecosim_clear"); }

int32_t ecosim_randomize(EcosimWorld*, const EcosimSeedConfig*) {
  return not_implemented("ecosim_randomize");
}

int32_t ecosim_stats(const EcosimWorld*, EcosimStats*) {
  return not_implemented("ecosim_stats");
}

uint64_t ecosim_hash(const EcosimWorld* world) {
  ecosim::clear_error();
  ecosim::World* w = as_world(const_cast<EcosimWorld*>(world));
  if (!w) return 0;

  // FNV-1a over the dimensions and the species plane: enough to prove two
  // runs agree cell for cell, cheap enough to call from a test on every case.
  uint64_t h = 1469598103934665603ull;
  auto mix = [&h](uint64_t v) {
    for (int i = 0; i < 8; ++i) {
      h = (h ^ ((v >> (i * 8)) & 0xFF)) * 1099511628211ull;
    }
  };
  mix(static_cast<uint64_t>(w->grid().w()));
  mix(static_cast<uint64_t>(w->grid().h()));
  const std::vector<uint8_t>& species = w->grid().cur().species;
  for (uint8_t s : species) h = (h ^ s) * 1099511628211ull;
  return h;
}

int32_t ecosim_rle_size(const EcosimWorld* world, int32_t* out_needed) {
  ecosim::clear_error();
  ecosim::World* w = as_world(const_cast<EcosimWorld*>(world));
  if (!w) return ECOSIM_ERR_INVALID;
  if (out_needed == nullptr) {
    ecosim::set_error("out_needed 为 NULL");
    return ECOSIM_ERR_INVALID;
  }
  *out_needed = ecosim::rle::worst_case_size(w->grid().w(), w->grid().h());
  return ECOSIM_OK;
}

int32_t ecosim_to_rle(const EcosimWorld* world, char* buf, int32_t cap, int32_t* out_written) {
  ecosim::clear_error();
  ecosim::World* w = as_world(const_cast<EcosimWorld*>(world));
  if (!w) return ECOSIM_ERR_INVALID;
  if (buf == nullptr || out_written == nullptr || cap <= 0) {
    ecosim::set_error("to_rle 需要非空缓冲区与 out_written");
    return ECOSIM_ERR_INVALID;
  }

  std::string text;
  const int32_t rc = ecosim::rle::encode(w->grid().cur(), w->grid().w(), w->grid().h(), w->rules(),
                                         text);
  if (rc != ECOSIM_OK) return rc;

  if (static_cast<int64_t>(text.size()) + 1 > cap) {
    char msg[128];
    std::snprintf(msg, sizeof(msg), "缓冲区 %d 字节不够，需要 %zu 字节（先调 ecosim_rle_size）",
                  cap, text.size() + 1);
    ecosim::set_error(msg);
    return ECOSIM_ERR_SIZE;
  }

  std::memcpy(buf, text.data(), text.size());
  buf[text.size()] = '\0';
  *out_written = static_cast<int32_t>(text.size()) + 1;
  return ECOSIM_OK;
}

static int32_t load_rle_at(EcosimWorld* world, const char* rle, int32_t origin_x,
                           int32_t origin_y) {
  ecosim::clear_error();
  ecosim::World* w = as_world(world);
  if (!w) return ECOSIM_ERR_INVALID;
  if (rle == nullptr) {
    ecosim::set_error("rle 为 NULL");
    return ECOSIM_ERR_INVALID;
  }

  int32_t pattern_w = 0;
  int32_t pattern_h = 0;
  std::string warning;
  const int32_t rc = ecosim::rle::decode(std::string_view(rle), w->grid().cur(), w->grid().w(),
                                         w->grid().h(), origin_x, origin_y, pattern_w, pattern_h,
                                         warning);
  if (rc != ECOSIM_OK) return rc;

  // Same rule as paint: the scratch buffer must not hold a stale generation.
  w->grid().copy_cur_to_nxt();
  return ECOSIM_OK;
}

int32_t ecosim_from_rle(EcosimWorld* world, const char* rle) {
  return load_rle_at(world, rle, 0, 0);
}

int32_t ecosim_from_rle_at(EcosimWorld* world, const char* rle, int32_t origin_x,
                           int32_t origin_y) {
  return load_rle_at(world, rle, origin_x, origin_y);
}

}  // extern "C"
