#include "core/params.h"

#include <cstdio>

namespace ecosim {

EcosimParams default_params() {
  EcosimParams p{};
  p.struct_size = sizeof(EcosimParams);
  p.light_permille = 350;
  p.fertility_permille = 200;
  for (int32_t i = 0; i < ECOSIM_MAX_SPECIES; ++i) {
    p.metabolism_scale_permille[i] = 1000;
    p.repro_scale_permille[i] = 1000;
  }
  return p;
}

bool validate_params(const EcosimParams& p, char* why, int32_t cap) {
  if (p.struct_size < static_cast<int32_t>(sizeof(EcosimParams))) {
    std::snprintf(why, cap, "EcosimParams.struct_size 太小");
    return false;
  }
  if (p.light_permille < 0 || p.light_permille > 1000) {
    std::snprintf(why, cap, "light %d 超出 0..1000", p.light_permille);
    return false;
  }
  if (p.fertility_permille < 0 || p.fertility_permille > 1000) {
    std::snprintf(why, cap, "fertility %d 超出 0..1000", p.fertility_permille);
    return false;
  }
  for (int32_t i = 0; i < ECOSIM_MAX_SPECIES; ++i) {
    if (p.metabolism_scale_permille[i] < 0 || p.metabolism_scale_permille[i] > 10000) {
      std::snprintf(why, cap, "metabolism_scale[%d] = %d 超出 0..10000", i,
                    p.metabolism_scale_permille[i]);
      return false;
    }
    if (p.repro_scale_permille[i] < 0 || p.repro_scale_permille[i] > 10000) {
      std::snprintf(why, cap, "repro_scale[%d] = %d 超出 0..10000", i,
                    p.repro_scale_permille[i]);
      return false;
    }
  }
  return true;
}

}  // namespace ecosim
