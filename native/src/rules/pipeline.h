#pragma once

#include <cstdint>
#include <vector>

#include "core/grid.h"
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

/// One ecosystem generation: sense -> feed -> metabolise -> reproduce ->
/// disperse -> regrow, in that order, each stage reading only the previous
/// stage's output. Single threaded on purpose for M2: the stages mutate the
/// buffer they read, so banding them would make results depend on the split.
/// The B/S path keeps its parallel kernel.
void run_ecosystem(World& w);

}  // namespace ecosim
