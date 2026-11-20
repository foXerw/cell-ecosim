#pragma once

#include <array>
#include <cstdint>

#include "core/grid.h"
#include "ecosim_abi.h"

namespace ecosim {

/// A birth/survive rule as two nine-entry tables indexed by live-neighbour
/// count. Conway's Life is B3/S23.
struct RuleTable {
  std::array<uint8_t, 9> birth{};
  std::array<uint8_t, 9> survive{};

  static RuleTable life();
  static RuleTable from_config(const EcosimConfig& cfg);
};

namespace kernel {

/// Reference implementation of the B/S pass: eight explicit neighbour probes
/// per cell, modulo arithmetic for the torus, out-of-range treated as dead for
/// a bounded grid. Reads `in`, writes `out`; the caller swaps afterwards.
/// Milestone M1 task 5 adds a bitmask/popcount fast path that must agree with
/// this one cell for cell.
void step_birsurv(const Planes& in, Planes& out, int32_t w, int32_t h, const RuleTable& rules,
                  int32_t topology);

int32_t count_alive(const Planes& planes);

}  // namespace kernel
}  // namespace ecosim
