#include "core/kernel_fast.h"

#include <algorithm>
#include <vector>

namespace ecosim {
namespace kernel {

namespace {

constexpr int32_t kDx[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
constexpr int32_t kDy[8] = {-1, -1, -1, 0, 0, 1, 1, 1};

struct PopcountTable {
  uint8_t value[256];
  constexpr PopcountTable() : value{} {
    for (int i = 0; i < 256; ++i) {
      int n = 0;
      for (int b = 0; b < 8; ++b) n += (i >> b) & 1;
      value[i] = static_cast<uint8_t>(n);
    }
  }
};

inline constexpr PopcountTable kPopcount{};

inline int32_t wrap(int32_t v, int32_t n) { return v < 0 ? n - 1 : (v >= n ? 0 : v); }

}  // namespace

int32_t popcount8(uint8_t mask) { return kPopcount.value[mask]; }

uint8_t neighbor_mask(const Planes& in, int32_t w, int32_t h, int32_t x, int32_t y,
                      int32_t topology) {
  const bool torus = topology == ECOSIM_TOPOLOGY_TORUS;
  uint8_t mask = 0;
  for (int32_t i = 0; i < 8; ++i) {
    int32_t sx = x + kDx[i];
    int32_t sy = y + kDy[i];
    if (torus) {
      sx = wrap(sx, w);
      sy = wrap(sy, h);
    } else if (sx < 0 || sy < 0 || sx >= w || sy >= h) {
      continue;
    }
    if (in.species[static_cast<size_t>(sy) * w + sx] != 0) {
      mask = static_cast<uint8_t>(mask | (1u << i));
    }
  }
  return mask;
}

void step_fast(const Planes& in, Planes& out, int32_t w, int32_t h, const RuleTable& rules,
               int32_t topology, int32_t* counts_out, EnergyHistogram* histogram_out) {
  step_fast(in, out, w, h, 0, h, rules, topology, counts_out, histogram_out);
}

void step_fast(const Planes& in, Planes& out, int32_t w, int32_t h, int32_t y_begin,
               int32_t y_end, const RuleTable& rules, int32_t topology, int32_t* counts_out,
               EnergyHistogram* histogram_out) {
  const bool torus = topology == ECOSIM_TOPOLOGY_TORUS;

  // Three rotating rows of 0/1 alive flags. Reading these instead of striding
  // over the species plane keeps the hot loop inside a few cache lines.
  std::vector<uint8_t> up(w), mid(w), down(w);

  auto load = [&](std::vector<uint8_t>& dst, int32_t y) {
    if (y < 0 || y >= h) {
      std::fill(dst.begin(), dst.end(), uint8_t{0});  // bounded edge: permanent death
      return;
    }
    const uint8_t* src = in.species.data() + static_cast<size_t>(y) * w;
    for (int32_t x = 0; x < w; ++x) dst[x] = src[x] != 0 ? 1 : 0;
  };

  if (y_begin >= y_end) return;

  load(up, torus ? wrap(y_begin - 1, h) : y_begin - 1);
  load(mid, y_begin);

  for (int32_t y = y_begin; y < y_end; ++y) {
    load(down, torus ? wrap(y + 1, h) : y + 1);

    const size_t row = static_cast<size_t>(y) * w;
    const uint8_t* in_species = in.species.data() + row;
    const uint16_t* in_age = in.age.data() + row;
    const uint16_t* in_energy = in.energy.data() + row;
    const uint8_t* in_resource = in.resource.data() + row;
    uint8_t* out_species = out.species.data() + row;
    uint16_t* out_age = out.age.data() + row;
    uint16_t* out_energy = out.energy.data() + row;
    uint8_t* out_resource = out.resource.data() + row;

    for (int32_t x = 0; x < w; ++x) {
      // Left and right neighbours without a modulo in the loop body.
      const int32_t xl = (x == 0) ? (torus ? w - 1 : -1) : x - 1;
      const int32_t xr = (x == w - 1) ? (torus ? 0 : -1) : x + 1;

      uint8_t mask = static_cast<uint8_t>((up[x] << 1) | (down[x] << 6));
      if (xl >= 0) {
        mask = static_cast<uint8_t>(mask | (up[xl] << 0) | (mid[xl] << 3) | (down[xl] << 5));
      }
      if (xr >= 0) {
        mask = static_cast<uint8_t>(mask | (up[xr] << 2) | (mid[xr] << 4) | (down[xr] << 7));
      }

      const int32_t live = kPopcount.value[mask];
      const uint8_t self = in_species[x];

      out_species[x] = self ? (rules.survive[live] ? self : 0)
                            : (rules.birth[live] ? static_cast<uint8_t>(1) : 0);
      out_age[x] = out_species[x]
                       ? static_cast<uint16_t>(self ? in_age[x] + 1 : 0)
                       : static_cast<uint16_t>(0);

      // Untouched until the ecosystem pipeline (M2), but they ride along with
      // the swap, so carry them forward explicitly.
      out_energy[x] = in_energy[x];
      out_resource[x] = in_resource[x];
      if (counts_out != nullptr && out_species[x] != 0) ++counts_out[out_species[x]];
      if (histogram_out != nullptr && out_species[x] != 0) {
        const int32_t bucket = energy_bucket(out_species[x], out_energy[x]);
        if (bucket >= 0) ++(*histogram_out)[bucket];
      }
    }

    std::swap(up, mid);
    std::swap(mid, down);
  }
}

}  // namespace kernel
}  // namespace ecosim
