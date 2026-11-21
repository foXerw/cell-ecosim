#include "rules/pipeline.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "core/parallel.h"
#include "core/params.h"
#include "core/species.h"
#include "core/world.h"
#include "util/rng.h"

namespace ecosim {

namespace {

constexpr int32_t kDx[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
constexpr int32_t kDy[8] = {-1, -1, -1, 0, 0, 1, 1, 1};

// Per-stage rng channels, so one cell's luck in feeding does not correlate with
// its luck in reproducing.
constexpr uint64_t kChannelRepro = 23;
constexpr uint64_t kChannelDisperse = 37;
constexpr uint64_t kChannelFertility = 41;
constexpr uint64_t kChannelLight = 53;

constexpr uint8_t kCarcassResource = 20;
constexpr uint8_t kSproutCost = 40;
constexpr uint8_t kSproutResourceFloor = 60;
constexpr uint16_t kChildSharePermille = 400;
constexpr uint8_t kCrowdingLimit = 5;

inline int32_t wrap(int32_t v, int32_t n) { return v < 0 ? n - 1 : (v >= n ? 0 : v); }

inline int64_t neighbour_index(int32_t x, int32_t y, int32_t k, int32_t w, int32_t h, bool torus) {
  int32_t nx = x + kDx[k];
  int32_t ny = y + kDy[k];
  if (torus) {
    nx = wrap(nx, w);
    ny = wrap(ny, h);
  } else if (nx < 0 || ny < 0 || nx >= w || ny >= h) {
    return -1;
  }
  return static_cast<int64_t>(ny) * w + nx;
}

/// What a stage that reaches across cells wants to do. Candidates are computed
/// in parallel from an untouched input buffer, then applied serially in cell
/// order: the result cannot depend on how the work was split across threads,
/// and no band boundary becomes an invisible wall.
struct FeedAction {
  int64_t predator;
  int64_t prey;
  uint16_t gain;
};

struct ReproAction {
  int64_t parent;
  int64_t slot;
  uint8_t species;
  uint16_t child_energy;
};

struct MoveAction {
  int64_t from;
  int64_t to;
  uint8_t species;
};

struct Ctx {
  const Planes* in;
  Planes* out;
  const SenseField* sf;
  const EcosimParams* p;
  int32_t w;
  int32_t h;
  bool torus;
  uint64_t generation;
  uint64_t seed;
};

void sense_rows(const Ctx& c, int32_t y0, int32_t y1) {
  SenseField& sf = const_cast<SenseField&>(*c.sf);
  for (int32_t y = y0; y < y1; ++y) {
    for (int32_t x = 0; x < c.w; ++x) {
      const size_t i = static_cast<size_t>(y) * c.w + x;
      uint16_t resource = 0;
      for (int32_t k = 0; k < 8; ++k) {
        const int64_t j = neighbour_index(x, y, k, c.w, c.h, c.torus);
        if (j < 0) continue;
        const uint8_t s = c.in->species[j];
        if (s >= 1 && s <= 3) ++sf.count[s - 1][i];
        resource += c.in->resource[j];
      }
      sf.resource_sum[i] = resource;
    }
  }
}

/// Producers convert surrounding resource into energy; consumers take the best
/// single prey they can see. Reads only, so it is band-safe.
void feed_candidates(const Ctx& c, int32_t y0, int32_t y1, std::vector<FeedAction>& out) {
  for (int32_t y = y0; y < y1; ++y) {
    for (int32_t x = 0; x < c.w; ++x) {
      const size_t i = static_cast<size_t>(y) * c.w + x;
      const uint8_t s = c.in->species[i];
      if (s == 0) continue;
      const SpeciesDef& def = species_of(s);
      if (def.feed_efficiency_permille == 0) continue;
      const uint8_t prey_species = static_cast<uint8_t>(s - 1);
      if (prey_species == 0) continue;

      int64_t best = -1;
      uint16_t best_energy = 0;
      for (int32_t k = 0; k < 8; ++k) {
        const int64_t j = neighbour_index(x, y, k, c.w, c.h, c.torus);
        if (j < 0 || c.in->species[j] != prey_species) continue;
        if (c.in->energy[j] > best_energy) {
          best_energy = c.in->energy[j];
          best = j;
        }
      }
      if (best < 0 || best_energy == 0) continue;

      uint32_t gain = static_cast<uint32_t>(best_energy) * def.feed_efficiency_permille / 1000u;
      if (gain == 0) gain = 1;
      out.push_back(FeedAction{
          static_cast<int64_t>(i), best,
          static_cast<uint16_t>(std::min<uint32_t>(gain, best_energy))});
    }
  }
}

void apply_feed(const Ctx& c, const std::vector<std::vector<FeedAction>>& per_band) {
  std::vector<uint8_t> drained(c.in->species.size(), 0);
  Planes& out = *c.out;
  for (const auto& band : per_band) {
    for (const FeedAction& a : band) {
      if (drained[a.prey]) continue;
      drained[a.prey] = 1;
      const SpeciesDef& def = species_of(out.species[a.predator]);
      out.energy[a.predator] = static_cast<uint16_t>(
          std::min<uint32_t>(out.energy[a.predator] + a.gain, def.max_energy));
      out.energy[a.prey] = static_cast<uint16_t>(out.energy[a.prey] - a.gain);
      if (out.energy[a.prey] == 0) {
        out.species[a.prey] = 0;
        out.age[a.prey] = 0;
        out.resource[a.prey] =
            static_cast<uint8_t>(std::min<int32_t>(255, out.resource[a.prey] + kCarcassResource));
      }
    }
  }
}

/// Per cell: photosynthesis, metabolism, ageing, starvation. Disjoint rows.
void metabolise_rows(const Ctx& c, int32_t y0, int32_t y1) {
  Planes& io = *c.out;
  for (int32_t y = y0; y < y1; ++y) {
    for (int32_t x = 0; x < c.w; ++x) {
      const size_t i = static_cast<size_t>(y) * c.w + x;
      const uint8_t s = io.species[i];
      if (s == 0) continue;
      const SpeciesDef& def = species_of(s);

      if (def.photosynthesis_permille > 0) {
        const uint32_t want = static_cast<uint32_t>(c.sf->resource_sum[i]) *
                              def.photosynthesis_permille / 8000u;
        const uint32_t take = std::min<uint32_t>(want, io.resource[i]);
        if (take > 0) {
          io.resource[i] = static_cast<uint8_t>(io.resource[i] - take);
          io.energy[i] = static_cast<uint16_t>(
              std::min<uint32_t>(io.energy[i] + take, def.max_energy));
        }
      }

      const uint32_t burn = std::max<uint32_t>(
          1, static_cast<uint32_t>(def.metabolism) * c.p->metabolism_scale_permille[s] / 1000u);
      if (io.energy[i] <= burn) {
        io.species[i] = 0;
        io.energy[i] = 0;
        io.age[i] = 0;
        io.resource[i] =
            static_cast<uint8_t>(std::min<int32_t>(255, io.resource[i] + kCarcassResource));
      } else {
        io.energy[i] = static_cast<uint16_t>(io.energy[i] - burn);
        io.age[i] = static_cast<uint16_t>(std::min<int32_t>(65535, io.age[i] + 1));
      }
    }
  }
}

void repro_candidates(const Ctx& c, int32_t y0, int32_t y1, std::vector<ReproAction>& out) {
  const Planes& cur = *c.out;
  for (int32_t y = y0; y < y1; ++y) {
    for (int32_t x = 0; x < c.w; ++x) {
      const size_t i = static_cast<size_t>(y) * c.w + x;
      const uint8_t s = cur.species[i];
      if (s == 0) continue;
      const SpeciesDef& def = species_of(s);
      const uint32_t threshold =
          static_cast<uint32_t>(def.repro_threshold) * c.p->repro_scale_permille[s] / 1000u;
      if (cur.energy[i] < threshold) continue;
      if (s <= 3 && c.sf->count[s - 1][i] >= kCrowdingLimit) continue;

      int64_t slot = -1;
      for (int32_t k = 0; k < 8; ++k) {
        const int64_t j = neighbour_index(x, y, k, c.w, c.h, c.torus);
        if (j >= 0 && cur.species[j] == 0) {
          slot = j;
          break;
        }
      }
      if (slot < 0) continue;

      const uint32_t child = static_cast<uint32_t>(cur.energy[i]) * kChildSharePermille / 1000u;
      if (child == 0) continue;

      uint8_t child_species = s;
      if (rng::draw(c.seed, c.generation, i * 8 + kChannelRepro) % 1000u < def.mutation_permille) {
        child_species = static_cast<uint8_t>(s % 3 + 1);
      }
      out.push_back(ReproAction{
          static_cast<int64_t>(i), slot, child_species, static_cast<uint16_t>(child)});
    }
  }
}

void apply_repro(const Ctx& c, const std::vector<std::vector<ReproAction>>& per_band) {
  std::vector<uint8_t> taken(c.in->species.size(), 0);
  Planes& io = *c.out;
  for (const auto& band : per_band) {
    for (const ReproAction& a : band) {
      // The preferred slot may have been claimed by a lower-indexed parent in
      // this same pass. Try the parent's other neighbours rather than giving up:
      // losing a child to a neighbour's timing would be an arbitrary rule.
      const int32_t px = static_cast<int32_t>(a.parent % c.w);
      const int32_t py = static_cast<int32_t>(a.parent / c.w);
      int64_t slot = -1;
      const int64_t preferred = a.slot;
      if (!taken[preferred] && io.species[preferred] == 0) {
        slot = preferred;
      } else {
        for (int32_t k = 0; k < 8; ++k) {
          const int64_t j = neighbour_index(px, py, k, c.w, c.h, c.torus);
          if (j >= 0 && !taken[j] && io.species[j] == 0) {
            slot = j;
            break;
          }
        }
      }
      if (slot < 0) continue;

      taken[slot] = 1;
      io.energy[a.parent] = static_cast<uint16_t>(io.energy[a.parent] - a.child_energy);
      io.species[slot] = a.species;
      io.energy[slot] = a.child_energy;
      io.age[slot] = 0;
    }
  }
}

void disperse_candidates(const Ctx& c, int32_t y0, int32_t y1, std::vector<MoveAction>& out) {
  const Planes& cur = *c.out;
  for (int32_t y = y0; y < y1; ++y) {
    for (int32_t x = 0; x < c.w; ++x) {
      const size_t i = static_cast<size_t>(y) * c.w + x;
      const uint8_t s = cur.species[i];
      if (s == 0) continue;
      const SpeciesDef& def = species_of(s);
      if (def.mobility_permille == 0) continue;
      if (rng::draw(c.seed, c.generation, i * 8 + kChannelDisperse) % 1000u >=
          def.mobility_permille) {
        continue;
      }
      int64_t slot = -1;
      for (int32_t k = 0; k < 8; ++k) {
        const int64_t j = neighbour_index(x, y, k, c.w, c.h, c.torus);
        if (j >= 0 && cur.species[j] == 0) {
          slot = j;
          break;
        }
      }
      if (slot < 0) continue;
      out.push_back(MoveAction{static_cast<int64_t>(i), slot, s});
    }
  }
}

void apply_disperse(const Ctx& c, const std::vector<std::vector<MoveAction>>& per_band) {
  Planes& io = *c.out;
  for (const auto& band : per_band) {
    for (const MoveAction& a : band) {
      // The mover may have been displaced or its target filled by an earlier
      // move in this same pass; either way the candidate is stale.
      if (io.species[a.from] != a.species) continue;
      if (io.species[a.to] != 0) continue;
      io.species[a.to] = a.species;
      io.energy[a.to] = io.energy[a.from];
      io.age[a.to] = io.age[a.from];
      io.species[a.from] = 0;
      io.energy[a.from] = 0;
      io.age[a.from] = 0;
    }
  }
}

void regrow_rows(const Ctx& c, int32_t y0, int32_t y1) {
  Planes& io = *c.out;
  for (int32_t y = y0; y < y1; ++y) {
    for (int32_t x = 0; x < c.w; ++x) {
      const size_t i = static_cast<size_t>(y) * c.w + x;
      if (io.resource[i] < 255 &&
          rng::draw(c.seed, c.generation, i * 8 + kChannelFertility) % 1000u <
              static_cast<uint64_t>(c.p->fertility_permille)) {
        ++io.resource[i];
      }
      if (io.species[i] != 0) continue;
      if (io.resource[i] < kSproutResourceFloor) continue;
      if (rng::draw(c.seed, c.generation, i * 8 + kChannelLight) % 1000u >=
          static_cast<uint64_t>(c.p->light_permille)) {
        continue;
      }
      io.species[i] = 1;
      io.energy[i] = species_of(1).initial_energy;
      io.age[i] = 0;
      io.resource[i] = static_cast<uint8_t>(io.resource[i] - kSproutCost);
    }
  }
}

}  // namespace

void SenseField::allocate(int32_t w, int32_t h) {
  this->w = w;
  this->h = h;
  const size_t n = static_cast<size_t>(w) * h;
  for (auto& c : count) c.assign(n, 0);
  resource_sum.assign(n, 0);
}

void sense(const Planes& in, int32_t w, int32_t h, int32_t topology, SenseField& out) {
  if (out.w != w || out.h != h) {
    out.allocate(w, h);
  } else {
    for (auto& c : out.count) std::fill(c.begin(), c.end(), uint8_t{0});
    std::fill(out.resource_sum.begin(), out.resource_sum.end(), uint16_t{0});
  }
  const Ctx c{&in, nullptr, &out, nullptr, w, h, topology == ECOSIM_TOPOLOGY_TORUS, 0, 0};
  sense_rows(c, 0, h);
}

void run_ecosystem(World& w) {
  Grid& grid = w.grid();
  const int32_t W = grid.w();
  const int32_t H = grid.h();
  parallel::Pool& pool = w.pool();
  const int32_t band_rows = parallel::band_rows_for(H, pool.size());
  const int32_t bands = (H + band_rows - 1) / band_rows;

  static thread_local SenseField sf;
  sf.allocate(W, H);  // also zeroes: the census accumulates

  Ctx c;
  c.in = &grid.cur();
  c.out = &grid.nxt();
  c.sf = &sf;
  c.p = &w.params();
  c.w = W;
  c.h = H;
  c.torus = w.topology() == ECOSIM_TOPOLOGY_TORUS;
  c.generation = w.generation();
  c.seed = w.config().rng_seed;

  // 1. sense, in bands: writes disjoint rows, reads only cur.
  pool.run_bands(H, band_rows, [&](int32_t y0, int32_t y1) { sense_rows(c, y0, y1); });

  // The stages below mutate nxt, so start from an exact copy of cur.
  grid.nxt() = grid.cur();

  // 2. feed: candidates in parallel, applied in cell order.
  std::vector<std::vector<FeedAction>> feed_actions(bands);
  pool.run_bands(H, band_rows, [&](int32_t y0, int32_t y1) {
    feed_candidates(c, y0, y1, feed_actions[y0 / band_rows]);
  });
  apply_feed(c, feed_actions);

  // 3. metabolise (and photosynthesis) per cell.
  pool.run_bands(H, band_rows, [&](int32_t y0, int32_t y1) { metabolise_rows(c, y0, y1); });

  // 4. reproduce: candidates from the post-metabolism board, applied in order
  // so a free cell is claimed by the lowest-indexed parent that wants it.
  std::vector<std::vector<ReproAction>> repro_actions(bands);
  pool.run_bands(H, band_rows, [&](int32_t y0, int32_t y1) {
    repro_candidates(c, y0, y1, repro_actions[y0 / band_rows]);
  });
  apply_repro(c, repro_actions);

  // 5. disperse.
  std::vector<std::vector<MoveAction>> move_actions(bands);
  pool.run_bands(H, band_rows, [&](int32_t y0, int32_t y1) {
    disperse_candidates(c, y0, y1, move_actions[y0 / band_rows]);
  });
  apply_disperse(c, move_actions);

  // 6. regrow per cell.
  pool.run_bands(H, band_rows, [&](int32_t y0, int32_t y1) { regrow_rows(c, y0, y1); });

  grid.swap_buffers();
}

}  // namespace ecosim
