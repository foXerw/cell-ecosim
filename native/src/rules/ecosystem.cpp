#include "rules/pipeline.h"

#include <algorithm>
#include <cstring>

#include "core/params.h"
#include "core/species.h"
#include "core/world.h"
#include "util/rng.h"

namespace ecosim {

namespace {

constexpr int32_t kDx[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
constexpr int32_t kDy[8] = {-1, -1, -1, 0, 0, 1, 1, 1};

// Stage-local rng channels: the same cell draws different values per stage, so
// a stage's luck never correlates with another's.
constexpr uint64_t kChannelFeed = 11;
constexpr uint64_t kChannelRepro = 23;
constexpr uint64_t kChannelDisperse = 37;
constexpr uint64_t kChannelFertility = 41;
constexpr uint64_t kChannelLight = 53;

constexpr uint8_t kCarcassResource = 20;
constexpr uint8_t kSproutCost = 40;
constexpr uint8_t kSproutResourceFloor = 60;
constexpr uint16_t kChildSharePermille = 400;

inline int32_t wrap(int32_t v, int32_t n) { return v < 0 ? n - 1 : (v >= n ? 0 : v); }

/// Neighbour index in fixed scan order, or -1 when outside a bounded grid.
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

}  // namespace

void SenseField::allocate(int32_t w, int32_t h) {
  this->w = w;
  this->h = h;
  const size_t n = static_cast<size_t>(w) * h;
  for (auto& c : count) c.assign(n, 0);
  resource_sum.assign(n, 0);
}

void sense(const Planes& in, int32_t w, int32_t h, int32_t topology, SenseField& out) {
  // Own the buffer's lifetime: a caller must not be able to segfault the engine
  // by passing a SenseField it forgot to size.
  if (out.w != w || out.h != h) {
    out.allocate(w, h);
  } else {
    for (auto& c : out.count) std::fill(c.begin(), c.end(), uint8_t{0});
    std::fill(out.resource_sum.begin(), out.resource_sum.end(), uint16_t{0});
  }

  const bool torus = topology == ECOSIM_TOPOLOGY_TORUS;
  for (int32_t y = 0; y < h; ++y) {
    for (int32_t x = 0; x < w; ++x) {
      const size_t i = static_cast<size_t>(y) * w + x;
      uint16_t resource = 0;
      for (int32_t k = 0; k < 8; ++k) {
        const int64_t j = neighbour_index(x, y, k, w, h, torus);
        if (j < 0) continue;
        const uint8_t s = in.species[j];
        if (s >= 1 && s <= 3) ++out.count[s - 1][i];
        resource += in.resource[j];
      }
      out.resource_sum[i] = resource;
    }
  }
}

void run_ecosystem(World& w) {
  Grid& grid = w.grid();
  const int32_t W = grid.w();
  const int32_t H = grid.h();
  const bool torus = w.topology() == ECOSIM_TOPOLOGY_TORUS;
  const EcosimParams& P = w.params();
  const uint64_t gen = w.generation();
  const uint64_t seed = w.config().rng_seed;
  const size_t n = grid.count();

  // Reused across generations so a big grid does not reallocate every step;
  // sense() is responsible for clearing it.
  static thread_local SenseField sf;
  sense(grid.cur(), W, H, w.topology(), sf);

  const Planes& in = grid.cur();
  Planes& out = grid.nxt();
  out = in;  // stages below mutate `out` in place or through aux

  // Which prey cells this generation's predators already claimed. Static so a
  // big grid does not reallocate every generation.
  static thread_local std::vector<uint8_t> eaten;
  eaten.assign(n, 0);

  // ---- feed: predators take one prey each, best energy first --------------
  for (size_t i = 0; i < n; ++i) {
    const uint8_t s = in.species[i];
    if (s == 0) continue;
    const SpeciesDef& def = species_of(s);
    if (def.photosynthesis_permille > 0) {
      // Producers do not hunt: they turn the resource around them into energy
      // and deplete their own cell's stock by what they took.
      const uint32_t want = static_cast<uint32_t>(sf.resource_sum[i]) *
                            def.photosynthesis_permille / 8000u;
      const uint32_t take = std::min<uint32_t>(want, out.resource[i]);
      if (take > 0) {
        out.resource[i] = static_cast<uint8_t>(out.resource[i] - take);
        out.energy[i] = static_cast<uint16_t>(
            std::min<uint32_t>(out.energy[i] + take, def.max_energy));
      }
      continue;
    }
    if (def.feed_efficiency_permille == 0) continue;
    const uint8_t prey_species = static_cast<uint8_t>(s - 1);
    if (prey_species == 0) continue;

    const int32_t x = static_cast<int32_t>(i % W);
    const int32_t y = static_cast<int32_t>(i / W);
    int64_t best = -1;
    uint16_t best_energy = 0;
    for (int32_t k = 0; k < 8; ++k) {
      const int64_t j = neighbour_index(x, y, k, W, H, torus);
      if (j < 0 || eaten[j]) continue;
      if (in.species[j] != prey_species) continue;
      if (in.energy[j] > best_energy) {
        best_energy = in.energy[j];
        best = j;
      }
    }
    if (best < 0 || best_energy == 0) continue;

    uint32_t gain = static_cast<uint32_t>(best_energy) * def.feed_efficiency_permille / 1000u;
    if (gain == 0) gain = 1;
    gain = std::min<uint32_t>(gain, best_energy);
    eaten[best] = 1;

    out.energy[i] = static_cast<uint16_t>(
        std::min<uint32_t>(out.energy[i] + gain, def.max_energy));
    out.energy[best] = static_cast<uint16_t>(best_energy - gain);
    if (out.energy[best] == 0) {
      out.species[best] = 0;
      out.age[best] = 0;
      out.resource[best] =
          static_cast<uint8_t>(std::min<int32_t>(255, out.resource[best] + kCarcassResource));
    }
  }

  // ---- metabolise: burn energy, age, starve -------------------------------
  for (size_t i = 0; i < n; ++i) {
    const uint8_t s = out.species[i];
    if (s == 0) continue;
    const SpeciesDef& def = species_of(s);
    const uint32_t burn = std::max<uint32_t>(
        1, static_cast<uint32_t>(def.metabolism) * P.metabolism_scale_permille[s] / 1000u);
    const uint32_t e = out.energy[i];
    if (e <= burn) {
      out.species[i] = 0;
      out.energy[i] = 0;
      out.age[i] = 0;
      out.resource[i] =
          static_cast<uint8_t>(std::min<int32_t>(255, out.resource[i] + kCarcassResource));
    } else {
      out.energy[i] = static_cast<uint16_t>(e - burn);
      out.age[i] = static_cast<uint16_t>(std::min<int32_t>(65535, in.age[i] + 1));
    }
  }

  // ---- reproduce: spend energy on one child in the first empty neighbour ---
  {
    Planes& aux = grid.aux();
    aux = out;
    for (size_t i = 0; i < n; ++i) {
      const uint8_t s = aux.species[i];
      if (s == 0) continue;
      const SpeciesDef& def = species_of(s);
      const uint32_t threshold =
          static_cast<uint32_t>(def.repro_threshold) * P.repro_scale_permille[s] / 1000u;
      if (aux.energy[i] < threshold) continue;
      // Crowding: a cell surrounded by its own kind does not reproduce. Keeps
      // one species from paving the board in a handful of generations.
      if (s >= 1 && s <= 3 && sf.count[s - 1][i] >= 5) continue;

      const int32_t x = static_cast<int32_t>(i % W);
      const int32_t y = static_cast<int32_t>(i / W);
      int64_t spot = -1;
      for (int32_t k = 0; k < 8; ++k) {
        const int64_t j = neighbour_index(x, y, k, W, H, torus);
        if (j >= 0 && aux.species[j] == 0) {
          spot = j;
          break;
        }
      }
      if (spot < 0) continue;

      const uint32_t child = static_cast<uint32_t>(aux.energy[i]) * kChildSharePermille / 1000u;
      if (child == 0) continue;
      aux.energy[i] = static_cast<uint16_t>(aux.energy[i] - child);
      aux.energy[spot] = static_cast<uint16_t>(child);
      aux.age[spot] = 0;
      uint8_t child_species = s;
      if (rng::draw(seed, gen, i * 8 + kChannelRepro) % 1000u < def.mutation_permille) {
        child_species = static_cast<uint8_t>(s % 3 + 1);
      }
      aux.species[spot] = child_species;
    }
    out = aux;
  }

  // ---- disperse: swap into the first empty neighbour -----------------------
  {
    Planes& aux = grid.aux();
    aux = out;
    for (size_t i = 0; i < n; ++i) {
      const uint8_t s = aux.species[i];
      if (s == 0) continue;
      const SpeciesDef& def = species_of(s);
      if (rng::draw(seed, gen, i * 8 + kChannelDisperse) % 1000u >= def.mobility_permille) continue;

      const int32_t x = static_cast<int32_t>(i % W);
      const int32_t y = static_cast<int32_t>(i / W);
      int64_t spot = -1;
      for (int32_t k = 0; k < 8; ++k) {
        const int64_t j = neighbour_index(x, y, k, W, H, torus);
        if (j >= 0 && aux.species[j] == 0) {
          spot = j;
          break;
        }
      }
      if (spot < 0) continue;
      aux.species[spot] = s;
      aux.energy[spot] = aux.energy[i];
      aux.age[spot] = aux.age[i];
      aux.species[i] = 0;
      aux.energy[i] = 0;
      aux.age[i] = 0;
    }
    out = aux;
  }

  // ---- regrow: resource trickles in, producers sprout ----------------------
  for (size_t i = 0; i < n; ++i) {
    if (out.resource[i] < 255 &&
        rng::draw(seed, gen, i * 8 + kChannelFertility) % 1000u <
            static_cast<uint64_t>(P.fertility_permille)) {
      ++out.resource[i];
    }
    if (out.species[i] != 0) continue;
    if (out.resource[i] < kSproutResourceFloor) continue;
    if (rng::draw(seed, gen, i * 8 + kChannelLight) % 1000u >=
        static_cast<uint64_t>(P.light_permille)) {
      continue;
    }
    out.species[i] = 1;
    out.energy[i] = species_of(1).initial_energy;
    out.age[i] = 0;
    out.resource[i] = static_cast<uint8_t>(out.resource[i] - kSproutCost);
  }

  grid.swap_buffers();
}

}  // namespace ecosim
