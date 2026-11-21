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

namespace {

constexpr int32_t kGlobal = 0;
constexpr int32_t kPerSpecies = 1;
constexpr int32_t kPermille = 0;
constexpr int32_t kScale = 1;

// The one list the UI reads. Order is the parameter index and is part of the
// ABI from here on, so append only.
const ParamDesc kDescs[] = {
    {0, kGlobal, 0, kPermille, 0, 1000, 350, "light", "光照（生产者萌发率）"},
    {1, kGlobal, 0, kPermille, 0, 1000, 200, "fertility", "地力（resource 恢复率）"},
    {2, kPerSpecies, 1, kScale, 0, 10000, 1000, "metabolism_alga", "代谢倍率·藻"},
    {3, kPerSpecies, 1, kScale, 0, 10000, 1000, "repro_alga", "繁殖阈值倍率·藻"},
    {4, kPerSpecies, 2, kScale, 0, 10000, 1000, "metabolism_grazer", "代谢倍率·虫"},
    {5, kPerSpecies, 2, kScale, 0, 10000, 1000, "repro_grazer", "繁殖阈值倍率·虫"},
    {6, kPerSpecies, 3, kScale, 0, 10000, 1000, "metabolism_hunter", "代谢倍率·兽"},
    {7, kPerSpecies, 3, kScale, 0, 10000, 1000, "repro_hunter", "繁殖阈值倍率·兽"},
};

}  // namespace

int32_t param_count() { return static_cast<int32_t>(sizeof(kDescs) / sizeof(kDescs[0])); }

const ParamDesc* param_desc(int32_t index) {
  if (index < 0 || index >= param_count()) return nullptr;
  return &kDescs[index];
}

int32_t param_get(const EcosimParams& p, int32_t index) {
  switch (index) {
    case 0: return p.light_permille;
    case 1: return p.fertility_permille;
    default: {
      const int32_t k = index - 2;
      const int32_t species = 1 + k / 2;
      if (species < 1 || species >= ECOSIM_MAX_SPECIES) return 0;
      return (k % 2 == 0) ? p.metabolism_scale_permille[species]
                          : p.repro_scale_permille[species];
    }
  }
}

bool param_set(EcosimParams& p, int32_t index, int32_t value) {
  const ParamDesc* d = param_desc(index);
  if (d == nullptr || value < d->min_value || value > d->max_value) return false;
  switch (index) {
    case 0: p.light_permille = value; return true;
    case 1: p.fertility_permille = value; return true;
    default: {
      const int32_t k = index - 2;
      const int32_t species = 1 + k / 2;
      if (species < 1 || species >= ECOSIM_MAX_SPECIES) return false;
      if (k % 2 == 0) p.metabolism_scale_permille[species] = value;
      else p.repro_scale_permille[species] = value;
      return true;
    }
  }
}

}  // namespace ecosim
