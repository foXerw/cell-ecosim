#pragma once

#include <cstdint>

#include "ecosim_abi.h"

namespace ecosim {

/// One row of the species table. Everything the ecosystem pipeline needs to
/// know about a species lives here, so adding species 4 is a table edit plus a
/// palette slot - not an engine change (design §8.2).
///
/// Fractional rates are stored in permille to stay in integers across the ABI.
struct SpeciesDef {
  int32_t id;                      // == index in the table, 1-based
  const char* name;
  uint16_t max_energy;
  uint16_t initial_energy;         // what a newborn / restored cell starts with
  uint16_t metabolism;             // energy burned per generation
  uint16_t repro_threshold;        // energy at which reproduction is attempted
  uint16_t feed_efficiency_permille;  // fraction of prey energy actually gained
  /// Producers only: energy gained per generation from the resource around the
  // cell, as permille of the 8-neighbour resource sum divided by 8.
  uint16_t photosynthesis_permille;
  uint16_t mobility_permille;         // chance per generation to swap with an empty neighbour
  uint16_t mutation_permille;         // chance a newborn is perturbed
};

/// The table, index 0 unused (0 means "empty cell").
const SpeciesDef* species_table();
int32_t species_count();  // highest usable id

const SpeciesDef& species_of(int32_t id);

}  // namespace ecosim
