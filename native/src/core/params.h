#pragma once

#include <cstdint>

#include "ecosim_abi.h"

namespace ecosim {

EcosimParams default_params();

/// Ranges are what keep the ecosystem from exploding or freezing instantly;
/// the UI sliders are generated from the same numbers (milestone M2 task 4).
bool validate_params(const EcosimParams& p, char* why, int32_t cap);

}  // namespace ecosim
