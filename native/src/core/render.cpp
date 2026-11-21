#include "core/render.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/species.h"
#include "util/error.h"

namespace ecosim {
namespace render {

namespace {

// The size EcosimPalette had before milestone M3 appended `layer`. A caller
// that passes this is read as "species layer", which is what it asked for.
constexpr int32_t kPaletteSizeBeforeLayers = 68;
constexpr int32_t kSproutCap = 96;  // matches kSproutResourceFloor in the pipeline

/// Heat ramp, in permille along the ramp. Stops are chosen so perceived
/// luminance rises monotonically *and* the middle of the range stays readable
/// on the near-black background: a heat map that is uniformly dark is worse
/// than no heat map at all.
struct Stop {
  int32_t at;
  uint8_t r, g, b;
};

constexpr Stop kStops[] = {
    {0, 30, 30, 90},      // deep blue
    {250, 60, 60, 180},    // blue
    {500, 60, 170, 220},   // cyan
    {750, 90, 220, 90},    // green
    {1000, 250, 240, 60},  // yellow
};

constexpr int32_t kStopCount = static_cast<int32_t>(sizeof(kStops) / sizeof(kStops[0]));

/// Gamma ~0.7 as a lookup table: the alternative is a pow() per cell, which at
/// 2048x2048 is four million transcendental calls per frame.
const std::array<int32_t, 1001>& gamma_curve() {
  static const std::array<int32_t, 1001> table = [] {
    std::array<int32_t, 1001> t{};
    for (int32_t i = 0; i <= 1000; ++i) {
      const double x = static_cast<double>(i) / 1000.0;
      t[i] = static_cast<int32_t>(std::pow(x, 0.7) * 1000.0 + 0.5);
    }
    return t;
  }();
  return table;
}

uint32_t heat_bgra(int32_t t_permille) {
  t_permille = std::clamp(gamma_curve()[std::clamp(t_permille, 0, 1000)], 0, 1000);
  int32_t segment = 0;
  while (segment + 1 < kStopCount - 1 && t_permille > kStops[segment + 1].at) ++segment;
  const Stop& a = kStops[segment];
  const Stop& b = kStops[segment + 1];
  const int32_t span = std::max(1, b.at - a.at);
  const int32_t f = t_permille - a.at;
  const uint32_t r = static_cast<uint32_t>(a.r + (b.r - a.r) * f / span);
  const uint32_t g = static_cast<uint32_t>(a.g + (b.g - a.g) * f / span);
  const uint32_t bl = static_cast<uint32_t>(a.b + (b.b - a.b) * f / span);
  return 0xFF000000u | (bl << 16) | (g << 8) | r;
}

inline int32_t luma_of(uint32_t bgra) {
  const int32_t b = static_cast<int32_t>(bgra & 0xFF);
  const int32_t g = static_cast<int32_t>((bgra >> 8) & 0xFF);
  const int32_t r = static_cast<int32_t>((bgra >> 16) & 0xFF);
  return (b * 114 + g * 587 + r * 299) / 1000;
}

uint32_t colour_for(uint8_t species, uint16_t energy, uint16_t age, uint8_t resource,
                    const EcosimPalette& pal, int32_t layer) {
  if (layer == ECOSIM_LAYER_SPECIES) {
    return pal.cell_bgra[species % ECOSIM_MAX_SPECIES];
  }
  if (species == 0) {
    // Empty ground stays the background on every heat map, otherwise a field
    // of nothing would read as a field of zero-energy cells.
    return pal.cell_bgra[0];
  }
  switch (layer) {
    case ECOSIM_LAYER_ENERGY: {
      const uint32_t max_energy = std::max<uint32_t>(1, species_of(species).max_energy);
      return heat_bgra(static_cast<int32_t>(energy * 1000ull / max_energy));
    }
    case ECOSIM_LAYER_RESOURCE:
      if (resource == 0) return pal.cell_bgra[0];
      // Normalised against the sprouting threshold, not 255: a board where
      // resource sits at 60 is a board about to bloom, and scaling by 255
      // would render that as near-black.
      return heat_bgra(std::min<int32_t>(resource, kSproutCap) * 1000 / kSproutCap);
    case ECOSIM_LAYER_AGE:
      return heat_bgra(std::min<int32_t>(age, 64) * 1000 / 64);
    default:
      return pal.cell_bgra[species % ECOSIM_MAX_SPECIES];
  }
}

}  // namespace

int32_t species_to_bgra(const uint8_t* species, int32_t w, int32_t h, uint8_t* bgra,
                        int32_t row_pitch, const EcosimPalette* palette, const uint16_t* energy,
                        const uint16_t* age, const uint8_t* resource) {
  if (species == nullptr || bgra == nullptr || palette == nullptr) {
    set_error("species_to_bgra 需要非空指针");
    return ECOSIM_ERR_INVALID;
  }
  if (palette->struct_size < kPaletteSizeBeforeLayers) {
    set_error("EcosimPalette.struct_size 太小");
    return ECOSIM_ERR_SIZE;
  }
  if (w <= 0 || h <= 0 || row_pitch < w * 4) {
    char msg[128];
    std::snprintf(msg, sizeof(msg), "非法的渲染目标 %dx%d pitch=%d", w, h, row_pitch);
    set_error(msg);
    return ECOSIM_ERR_INVALID;
  }

  // Older callers simply do not carry the field.
  const int32_t layer = palette->struct_size >= static_cast<int32_t>(sizeof(EcosimPalette))
                            ? palette->layer
                            : ECOSIM_LAYER_SPECIES;
  const bool needs_planes = layer != ECOSIM_LAYER_SPECIES;
  if (needs_planes && (energy == nullptr || age == nullptr || resource == nullptr)) {
    set_error("热力叠加层需要 energy/age/resource 三个平面");
    return ECOSIM_ERR_INVALID;
  }

  for (int32_t y = 0; y < h; ++y) {
    const size_t row = static_cast<size_t>(y) * w;
    uint8_t* out = bgra + static_cast<size_t>(y) * row_pitch;
    for (int32_t x = 0; x < w; ++x) {
      const uint8_t s = species[row + x];
      const uint32_t colour = needs_planes
                                  ? colour_for(s, energy[row + x], age[row + x],
                                               resource[row + x], *palette, layer)
                                  : palette->cell_bgra[s % ECOSIM_MAX_SPECIES];
      std::memcpy(out + static_cast<size_t>(x) * 4, &colour, sizeof(colour));
    }
  }
  return ECOSIM_OK;
}

}  // namespace render
}  // namespace ecosim
