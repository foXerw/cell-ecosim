#pragma once

#include <cstdint>

#include "core/grid.h"
#include "core/kernel.h"

namespace ecosim {
namespace kernel {

/// Eight neighbour flags packed into one byte, self excluded:
///   bit0 (-1,-1) bit1 (0,-1) bit2 (+1,-1)
///   bit3 (-1, 0)            bit4 (+1, 0)
///   bit5 (-1,+1) bit6 (0,+1) bit7 (+1,+1)
/// A neighbour outside a bounded grid contributes 0 (permanent death); on a
/// torus it wraps.
uint8_t neighbor_mask(const Planes& in, int32_t w, int32_t h, int32_t x, int32_t y,
                      int32_t topology);

/// Table lookup rather than a branch per bit; the table is 256 bytes and stays
/// in L1 for the whole pass.
int32_t popcount8(uint8_t mask);

/// Same contract as kernel::step_birsurv() and proven cell-for-cell equal to it
/// by tests/test_fast_kernel.cpp. The difference is how it gets there: three
/// rotating rows of 0/1 flags (so the hot loop reads cache-hot bytes instead of
/// striding over the plane), no modulo inside the row, and a table popcount.
void step_fast(const Planes& in, Planes& out, int32_t w, int32_t h, const RuleTable& rules,
               int32_t topology);

}  // namespace kernel
}  // namespace ecosim
