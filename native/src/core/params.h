#pragma once

#include <cstdint>

#include "ecosim_abi.h"

namespace ecosim {

EcosimParams default_params();

/// Ranges are what keep the ecosystem from exploding or freezing instantly;
/// the UI sliders are generated from the same numbers (milestone M2 task 4).
bool validate_params(const EcosimParams& p, char* why, int32_t cap);

/// Description of one tunable. The UI builds a slider per entry, so adding a
/// parameter in C++ needs no C# change beyond the struct itself (design §11.2).
struct ParamDesc {
  int32_t index;
  int32_t group;     // 0 = global, 1 = per species
  int32_t species;   // 0 when global, else the species id it belongs to
  int32_t kind;      // 0 = permille 0..1000, 1 = scale 0..10000
  int32_t min_value;
  int32_t max_value;
  int32_t default_value;
  const char* name;  // static UTF-8, for logs and tests
  const char* label; // static UTF-8, what the UI shows
};

int32_t param_count();
const ParamDesc* param_desc(int32_t index);  // nullptr when out of range
int32_t param_get(const EcosimParams& p, int32_t index);
bool param_set(EcosimParams& p, int32_t index, int32_t value);

}  // namespace ecosim
