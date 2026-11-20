#include "core/species.h"

namespace ecosim {

namespace {

// Starting values from design §8.2. They are deliberately round numbers: the
// ecosystem is tuned by feel in milestone M2 task 6, and round numbers make the
// hand-computed pipeline tests readable.
constexpr SpeciesDef kTable[] = {
    {0, "empty", 0, 0, 0, 0, 0, 0, 0},
    {1, "alga", 240, 80, 1, 120, 0, 0, 2},
    {2, "grazer", 400, 150, 3, 240, 500, 250, 2},
    {3, "hunter", 800, 300, 5, 480, 700, 350, 1},
};

}  // namespace

const SpeciesDef* species_table() { return kTable; }

int32_t species_count() { return 3; }

const SpeciesDef& species_of(int32_t id) {
  if (id < 0 || id > species_count()) id = 0;
  return kTable[id];
}

}  // namespace ecosim
