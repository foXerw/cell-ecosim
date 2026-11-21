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

#include "core/params.h"
#include "core/cew.h"
#include "core/render.h"
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
static_assert(sizeof(EcosimPalette) == 72, "EcosimPalette layout drift");
static_assert(offsetof(EcosimPalette, layer) == 68, "palette layer offset drift");
static_assert(sizeof(EcosimPaintOp) == 24, "EcosimPaintOp layout drift");
static_assert(sizeof(EcosimStats) == 88, "EcosimStats layout drift");
static_assert(offsetof(EcosimStats, generation) == 4, "generation offset drift");
static_assert(offsetof(EcosimStats, species_count) == 24, "species_count offset drift");
static_assert(sizeof(EcosimSeedConfig) == 20, "EcosimSeedConfig layout drift");
static_assert(sizeof(EcosimParams) == 140, "EcosimParams layout drift");
static_assert(sizeof(EcosimParamDesc) == 48, "EcosimParamDesc layout drift");
static_assert(sizeof(EcosimHistorySample) == 80, "EcosimHistorySample layout drift");
static_assert(sizeof(EcosimEvent) == 24, "EcosimEvent layout drift");

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
    case ECOSIM_STRUCT_PARAMS: return static_cast<int32_t>(sizeof(EcosimParams));
    case ECOSIM_STRUCT_PARAM_DESC: return static_cast<int32_t>(sizeof(EcosimParamDesc));
    case ECOSIM_STRUCT_HISTORY_SAMPLE: return static_cast<int32_t>(sizeof(EcosimHistorySample));
    case ECOSIM_STRUCT_EVENT: return static_cast<int32_t>(sizeof(EcosimEvent));
    default: return 0;
  }
}

const char* ecosim_last_error(void) { return ecosim::current_error(); }

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
  // A single catch-all on purpose: catching std::exception by type makes this
  // TU emit a strong definition of its typeinfo under -static, and the engine
  // objects emit the same one - a multiple-definition link error whose message
  // this toolchain never shows. The diagnostic is not lost: World::validate
  // already put it in the thread-local error string before throwing.
  try {
    return reinterpret_cast<EcosimWorld*>(new ecosim::World(*cfg));
  } catch (...) {
    if (std::strlen(ecosim_last_error()) == 0) {
      ecosim::set_error("创建世界时发生未知错误");
    }
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

int32_t ecosim_render_frame(const EcosimWorld* world, uint8_t* bgra, int32_t row_pitch,
                            const EcosimPalette* palette) {
  ecosim::clear_error();
  ecosim::World* w = as_world(const_cast<EcosimWorld*>(world));
  if (!w) return ECOSIM_ERR_INVALID;
  return w->render_frame(bgra, row_pitch, palette);
}

int32_t ecosim_render_species(const uint8_t* species, int32_t w, int32_t h, uint8_t* bgra,
                              int32_t row_pitch, const EcosimPalette* palette) {
  ecosim::clear_error();
  // A bare plane has no energy/age/resource to consult, so heat layers are
  // refused by the callee rather than silently rendering the species colours.
  return ecosim::render::species_to_bgra(species, w, h, bgra, row_pitch, palette, nullptr,
                                         nullptr, nullptr);
}

int32_t ecosim_render_planes(const uint8_t* species, const uint16_t* energy,
                             const uint16_t* age, const uint8_t* resource, int32_t w, int32_t h,
                             uint8_t* bgra, int32_t row_pitch, const EcosimPalette* palette) {
  ecosim::clear_error();
  return ecosim::render::species_to_bgra(species, w, h, bgra, row_pitch, palette, energy, age,
                                         resource);
}

int32_t ecosim_paint(EcosimWorld* world, const EcosimPaintOp* ops, int32_t count) {
  ecosim::clear_error();
  ecosim::World* w = as_world(world);
  if (!w) return ECOSIM_ERR_INVALID;
  return w->paint(ops, count);
}

int32_t ecosim_clear(EcosimWorld* world, int32_t fill_species) {
  ecosim::clear_error();
  ecosim::World* w = as_world(world);
  if (!w) return ECOSIM_ERR_INVALID;
  return w->clear(fill_species);
}

int32_t ecosim_load_species(EcosimWorld* world, const uint8_t* species, int32_t count) {
  ecosim::clear_error();
  ecosim::World* w = as_world(world);
  if (!w) return ECOSIM_ERR_INVALID;
  return w->load_species(species, count);
}

int32_t ecosim_randomize(EcosimWorld* world, const EcosimSeedConfig* seed_cfg) {
  ecosim::clear_error();
  ecosim::World* w = as_world(world);
  if (!w) return ECOSIM_ERR_INVALID;
  if (seed_cfg == nullptr) {
    ecosim::set_error("seed_cfg 为 NULL");
    return ECOSIM_ERR_INVALID;
  }
  return w->randomize(*seed_cfg);
}

int32_t ecosim_param_count(void) { return ecosim::param_count(); }

int32_t ecosim_param_desc(int32_t index, EcosimParamDesc* out) {
  ecosim::clear_error();
  if (out == nullptr) {
    ecosim::set_error("out 为 NULL");
    return ECOSIM_ERR_INVALID;
  }
  if (out->struct_size < static_cast<int32_t>(sizeof(EcosimParamDesc))) {
    ecosim::set_error("EcosimParamDesc.struct_size 太小");
    return ECOSIM_ERR_SIZE;
  }
  const ecosim::ParamDesc* d = ecosim::param_desc(index);
  if (d == nullptr) {
    char msg[96];
    std::snprintf(msg, sizeof(msg), "参数索引 %d 不存在（共 %d 个）", index,
                  ecosim::param_count());
    ecosim::set_error(msg);
    return ECOSIM_ERR_RANGE;
  }
  out->index = d->index;
  out->group = d->group;
  out->species = d->species;
  out->kind = d->kind;
  out->min_value = d->min_value;
  out->max_value = d->max_value;
  out->default_value = d->default_value;
  out->name = d->name;
  out->label = d->label;
  return ECOSIM_OK;
}

int32_t ecosim_params_get(const EcosimWorld* world, EcosimParams* out) {
  ecosim::clear_error();
  ecosim::World* w = as_world(const_cast<EcosimWorld*>(world));
  if (!w) return ECOSIM_ERR_INVALID;
  if (out == nullptr) {
    ecosim::set_error("out 为 NULL");
    return ECOSIM_ERR_INVALID;
  }
  if (out->struct_size < static_cast<int32_t>(sizeof(EcosimParams))) {
    ecosim::set_error("EcosimParams.struct_size 太小");
    return ECOSIM_ERR_SIZE;
  }
  *out = w->params();
  return ECOSIM_OK;
}

int32_t ecosim_param_get(const EcosimWorld* world, int32_t index, int32_t* out_value) {
  ecosim::clear_error();
  ecosim::World* w = as_world(const_cast<EcosimWorld*>(world));
  if (!w) return ECOSIM_ERR_INVALID;
  return w->get_param(index, out_value);
}

int32_t ecosim_param_set(EcosimWorld* world, int32_t index, int32_t value) {
  ecosim::clear_error();
  ecosim::World* w = as_world(world);
  if (!w) return ECOSIM_ERR_INVALID;
  return w->set_param(index, value);
}

int32_t ecosim_params_set(EcosimWorld* world, const EcosimParams* params) {
  ecosim::clear_error();
  ecosim::World* w = as_world(world);
  if (!w) return ECOSIM_ERR_INVALID;
  if (params == nullptr) {
    ecosim::set_error("params 为 NULL");
    return ECOSIM_ERR_INVALID;
  }
  return w->set_params(*params);
}

int32_t ecosim_history_count(const EcosimWorld* world) {
  ecosim::clear_error();
  ecosim::World* w = as_world(const_cast<EcosimWorld*>(world));
  return w ? w->history().size() : 0;
}

int32_t ecosim_history_get(const EcosimWorld* world, int32_t index, EcosimHistorySample* out) {
  ecosim::clear_error();
  ecosim::World* w = as_world(const_cast<EcosimWorld*>(world));
  if (!w) return ECOSIM_ERR_INVALID;
  if (out == nullptr) {
    ecosim::set_error("out 为 NULL");
    return ECOSIM_ERR_INVALID;
  }
  if (out->struct_size < static_cast<int32_t>(sizeof(EcosimHistorySample))) {
    ecosim::set_error("EcosimHistorySample.struct_size 太小");
    return ECOSIM_ERR_SIZE;
  }
  if (index < 0 || index >= w->history().size()) {
    char msg[96];
    std::snprintf(msg, sizeof(msg), "历史索引 %d 超出 0..%d", index, w->history().size() - 1);
    ecosim::set_error(msg);
    return ECOSIM_ERR_RANGE;
  }
  const ecosim::HistoryRing::Sample& s = w->history().at(index);
  out->generation = s.generation;
  out->alive_count = s.alive;
  std::memcpy(out->species_count, s.counts.data(), sizeof(out->species_count));
  return ECOSIM_OK;
}

int32_t ecosim_snapshot_count(const EcosimWorld* world) {
  ecosim::clear_error();
  ecosim::World* w = as_world(const_cast<EcosimWorld*>(world));
  return w ? w->snapshots().count() : 0;
}

int32_t ecosim_snapshot_info(const EcosimWorld* world, int32_t index,
                             uint64_t* out_generation) {
  ecosim::clear_error();
  ecosim::World* w = as_world(const_cast<EcosimWorld*>(world));
  if (!w) return ECOSIM_ERR_INVALID;
  if (out_generation == nullptr) {
    ecosim::set_error("out_generation 为 NULL");
    return ECOSIM_ERR_INVALID;
  }
  if (index < 0 || index >= w->snapshots().count()) {
    char msg[96];
    std::snprintf(msg, sizeof(msg), "快照序号 %d 超出 0..%d", index, w->snapshots().count() - 1);
    ecosim::set_error(msg);
    return ECOSIM_ERR_RANGE;
  }
  *out_generation = w->snapshots().generation_at(index);
  return ECOSIM_OK;
}

int32_t ecosim_snapshot_capture(EcosimWorld* world) {
  ecosim::clear_error();
  ecosim::World* w = as_world(world);
  if (!w) return ECOSIM_ERR_INVALID;
  return w->snapshots().capture(w->generation(), w->grid());
}

int32_t ecosim_snapshot_restore(EcosimWorld* world, int32_t index) {
  ecosim::clear_error();
  ecosim::World* w = as_world(world);
  if (!w) return ECOSIM_ERR_INVALID;
  return w->restore_snapshot(index);
}

int32_t ecosim_energy_histogram(const EcosimWorld* world, int32_t buckets, int32_t* out,
                                 int32_t out_capacity) {
  ecosim::clear_error();
  ecosim::World* w = as_world(const_cast<EcosimWorld*>(world));
  if (!w) return ECOSIM_ERR_INVALID;
  if (out == nullptr || out_capacity <= 0) {
    ecosim::set_error("energy_histogram 需要输出缓冲与正数容量");
    return ECOSIM_ERR_INVALID;
  }
  if (buckets < 2 || buckets > out_capacity) {
    char msg[128];
    std::snprintf(msg, sizeof(msg), "buckets %d 需要落在 2..%d（输出容量）之间", buckets,
                  out_capacity);
    ecosim::set_error(msg);
    return ECOSIM_ERR_RANGE;
  }

  const ecosim::EnergyHistogram& src = w->last_histogram();
  const int32_t internal = ecosim::kHistogramBuckets;
  for (int32_t i = 0; i < buckets; ++i) out[i] = 0;
  // Fold 16 buckets into N by summing every internal bucket whose midpoint
  // falls inside the requested one. Exact enough for bars, and no allocation.
  for (int32_t b = 0; b < internal; ++b) {
    const int32_t target = static_cast<int32_t>(
        (static_cast<int64_t>(b) * buckets + internal / 2) / internal);
    out[std::min(target, buckets - 1)] += src[b];
  }
  return ECOSIM_OK;
}

int32_t ecosim_event_poll(EcosimWorld* world, EcosimEvent* out, int32_t cap,
                          int32_t* out_count) {
  ecosim::clear_error();
  ecosim::World* w = as_world(world);
  if (!w) return ECOSIM_ERR_INVALID;
  if (out_count == nullptr) {
    ecosim::set_error("out_count 为 NULL");
    return ECOSIM_ERR_INVALID;
  }
  if (out == nullptr || cap < 0) {
    ecosim::set_error("event_poll 需要缓冲区与非负 cap");
    return ECOSIM_ERR_INVALID;
  }
  if (cap > 0 && out->struct_size < static_cast<int32_t>(sizeof(EcosimEvent))) {
    ecosim::set_error("EcosimEvent.struct_size 太小");
    return ECOSIM_ERR_SIZE;
  }
  *out_count = w->events().poll(out, cap);
  return ECOSIM_OK;
}

int32_t ecosim_stats(const EcosimWorld* world, EcosimStats* out) {
  ecosim::clear_error();
  ecosim::World* w = as_world(const_cast<EcosimWorld*>(world));
  if (!w) return ECOSIM_ERR_INVALID;
  if (out == nullptr) {
    ecosim::set_error("out 为 NULL");
    return ECOSIM_ERR_INVALID;
  }
  return w->stats(*out);
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

int32_t ecosim_cew_size(const EcosimWorld* world, int32_t* out_needed) {
  ecosim::clear_error();
  ecosim::World* w = as_world(const_cast<EcosimWorld*>(world));
  if (!w) return ECOSIM_ERR_INVALID;
  if (out_needed == nullptr) {
    ecosim::set_error("out_needed 为 NULL");
    return ECOSIM_ERR_INVALID;
  }
  *out_needed = ecosim::cew::worst_case_size(w->grid().w(), w->grid().h());
  return ECOSIM_OK;
}

int32_t ecosim_save_cew(const EcosimWorld* world, uint8_t* buf, int32_t cap,
                        int32_t* out_written) {
  ecosim::clear_error();
  ecosim::World* w = as_world(const_cast<EcosimWorld*>(world));
  if (!w) return ECOSIM_ERR_INVALID;
  if (buf == nullptr || out_written == nullptr || cap <= 0) {
    ecosim::set_error("save_cew 需要非空缓冲区与 out_written");
    return ECOSIM_ERR_INVALID;
  }
  std::vector<uint8_t> bytes;
  const int32_t rc = ecosim::cew::encode(*w, bytes);
  if (rc != ECOSIM_OK) return rc;
  if (static_cast<int64_t>(bytes.size()) > cap) {
    char msg[128];
    std::snprintf(msg, sizeof(msg), "缓冲区 %d 字节不够，存档需要 %zu 字节（先调 ecosim_cew_size）",
                  cap, bytes.size());
    ecosim::set_error(msg);
    return ECOSIM_ERR_SIZE;
  }
  std::memcpy(buf, bytes.data(), bytes.size());
  *out_written = static_cast<int32_t>(bytes.size());
  return ECOSIM_OK;
}

int32_t ecosim_load_cew(EcosimWorld* world, const uint8_t* buf, int32_t len) {
  ecosim::clear_error();
  ecosim::World* w = as_world(world);
  if (!w) return ECOSIM_ERR_INVALID;
  if (buf == nullptr || len <= 0) {
    ecosim::set_error("load_cew 需要非空缓冲与正数长度");
    return ECOSIM_ERR_INVALID;
  }
  std::string why;
  const int32_t rc = ecosim::cew::decode(buf, static_cast<size_t>(len), *w, why);
  if (rc != ECOSIM_OK && !why.empty()) ecosim::set_error(why.c_str());
  return rc;
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
