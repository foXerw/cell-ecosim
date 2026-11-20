#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "core/grid.h"
#include "core/kernel.h"

namespace ecosim {
namespace rle {

/// Standard RLE (Golly-compatible, single state: 'b' dead, 'o' alive).
/// Comments starting with '#' and all whitespace are ignored.
///
/// Encoding uses the bounding box of the live cells, which is what other tools
/// write; an empty grid becomes a 1x1 box.
int32_t encode(const Planes& planes, int32_t w, int32_t h, const RuleTable& rules,
               std::string& out);

/// Decodes into `planes`, which must already be sized w x h. The pattern's
/// top-left corner lands at (origin_x, origin_y); anything outside the grid is
/// clipped and reported through `warning` (that is not an error: see design
/// appendix A.4.3). `out_w`/`out_h` receive the pattern's own dimensions.
int32_t decode(std::string_view text, Planes& planes, int32_t w, int32_t h, int32_t origin_x,
               int32_t origin_y, int32_t& out_w, int32_t& out_h, std::string& warning);

/// Largest buffer encode() could need for this grid, including the NUL.
int32_t worst_case_size(int32_t w, int32_t h);

}  // namespace rle
}  // namespace ecosim
