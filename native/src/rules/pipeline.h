#pragma once

#include <cstdint>
#include <vector>

#include "core/grid.h"
#include "core/history.h"
#include "ecosim_abi.h"

namespace ecosim {

class World;

/// Per-cell neighbour census for the ecosystem stages: how many of the eight
/// neighbours belong to each tracked species, and how much resource surrounds
/// the cell. Counts cap at 8 by construction, so a byte each is enough.
struct SenseField {
  int32_t w = 0;
  int32_t h = 0;
  std::vector<uint8_t> count[3];  // index = species id - 1
  std::vector<uint16_t> resource_sum;

  void allocate(int32_t w, int32_t h);
};

void sense(const Planes& in, int32_t w, int32_t h, int32_t topology, SenseField& out);

/// Runs one ecosystem generation and reports the census of the resulting
/// board. The caller (World) records it: the pipeline must not call back into
/// World's out-of-line members, because that would create a cycle inside
/// libecosim_core.a, and ld resolves archives in a single pass.
void run_ecosystem(World& w, SpeciesCounts* counts_out, EnergyHistogram* histogram_out);

}  // namespace ecosim
