#include "core/render.h"

#include <cstdio>
#include <cstring>

#include "util/error.h"

namespace ecosim {
namespace render {

int32_t species_to_bgra(const uint8_t* species, int32_t w, int32_t h, uint8_t* bgra,
                        int32_t row_pitch, const EcosimPalette* palette) {
  if (species == nullptr || bgra == nullptr || palette == nullptr) {
    set_error("species_to_bgra 需要非空指针");
    return ECOSIM_ERR_INVALID;
  }
  if (palette->struct_size < static_cast<int32_t>(sizeof(EcosimPalette))) {
    set_error("EcosimPalette.struct_size 太小");
    return ECOSIM_ERR_SIZE;
  }
  if (w <= 0 || h <= 0 || row_pitch < w * 4) {
    char msg[128];
    std::snprintf(msg, sizeof(msg), "非法的渲染目标 %dx%d pitch=%d", w, h, row_pitch);
    set_error(msg);
    return ECOSIM_ERR_INVALID;
  }

  for (int32_t y = 0; y < h; ++y) {
    const uint8_t* row = species + static_cast<size_t>(y) * w;
    uint8_t* out = bgra + static_cast<size_t>(y) * row_pitch;
    for (int32_t x = 0; x < w; ++x) {
      const uint32_t colour = palette->cell_bgra[row[x] % ECOSIM_MAX_SPECIES];
      std::memcpy(out + static_cast<size_t>(x) * 4, &colour, sizeof(colour));
    }
  }
  return ECOSIM_OK;
}

}  // namespace render
}  // namespace ecosim
