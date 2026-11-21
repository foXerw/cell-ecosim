#pragma once

#include <cstdint>

#include "ecosim_abi.h"

namespace ecosim {
namespace render {

/// Paints one BGRA8 pixel per cell from a bare species plane.
///
/// Deliberately takes no World: the UI renders from an immutable snapshot that
/// the clock thread copied right after a step, so a repaint never waits on a
/// long simulation batch holding the engine lock (and vice versa).
int32_t species_to_bgra(const uint8_t* species, int32_t w, int32_t h, uint8_t* bgra,
                        int32_t row_pitch, const EcosimPalette* palette);

}  // namespace render
}  // namespace ecosim
