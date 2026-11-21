#include "check.h"

#include <cstring>

#include "core/params.h"
#include "ecosim_abi.h"

namespace {

EcosimWorld* eco_world() {
  EcosimConfig cfg{};
  cfg.struct_size = sizeof(EcosimConfig);
  cfg.w = 16;
  cfg.h = 16;
  cfg.mode = ECOSIM_MODE_ECOSYSTEM;
  cfg.topology = ECOSIM_TOPOLOGY_BOUNDED;
  cfg.threads = 1;
  cfg.rng_seed = 3;
  return ecosim_world_create(&cfg);
}

}  // namespace

ECOSIM_TEST(param_table_is_self_consistent) {
  const int32_t n = ecosim_param_count();
  CHECK_TRUE(n >= 2);
  for (int32_t i = 0; i < n; ++i) {
    EcosimParamDesc d{};
    d.struct_size = sizeof(d);
    CHECK_EQ_i32(ECOSIM_OK, ecosim_param_desc(i, &d));
    CHECK_EQ_i32(i, d.index);
    CHECK_TRUE(d.min_value <= d.default_value);
    CHECK_TRUE(d.default_value <= d.max_value);
    CHECK_TRUE(d.name != nullptr && std::strlen(d.name) > 0);
    CHECK_TRUE(d.label != nullptr && std::strlen(d.label) > 0);
    if (d.group == 1) {
      CHECK_TRUE(d.species >= 1 && d.species <= 3);
    } else {
      CHECK_EQ_i32(0, d.species);
    }
  }

  EcosimParamDesc beyond{};
  beyond.struct_size = sizeof(beyond);
  CHECK_EQ_i32(ECOSIM_ERR_RANGE, ecosim_param_desc(n, &beyond));
  EcosimParamDesc tiny{};
  tiny.struct_size = 4;
  CHECK_EQ_i32(ECOSIM_ERR_SIZE, ecosim_param_desc(0, &tiny));
  CHECK_EQ_i32(ECOSIM_ERR_INVALID, ecosim_param_desc(0, nullptr));
}

ECOSIM_TEST(every_parameter_round_trips_through_the_abi) {
  EcosimWorld* w = eco_world();
  EcosimParams p{};
  p.struct_size = sizeof(p);
  CHECK_EQ_i32(ECOSIM_OK, ecosim_params_get(w, &p));

  const int32_t n = ecosim_param_count();
  for (int32_t i = 0; i < n; ++i) {
    EcosimParamDesc d{};
    d.struct_size = sizeof(d);
    CHECK_EQ_i32(ECOSIM_OK, ecosim_param_desc(i, &d));

    for (const int32_t value : {d.min_value, d.default_value, d.max_value}) {
      EcosimParams probe = p;
      CHECK_TRUE(ecosim::param_set(probe, i, value));
      CHECK_EQ_i32(ECOSIM_OK, ecosim_params_set(w, &probe));

      EcosimParams read{};
      read.struct_size = sizeof(read);
      CHECK_EQ_i32(ECOSIM_OK, ecosim_params_get(w, &read));
      CHECK_EQ_i32(value, ecosim::param_get(read, i));
      p = read;
    }
  }

  // Out of range must be refused, and the world must keep its old values.
  EcosimParams bad = p;
  const int32_t light = bad.light_permille;
  CHECK_TRUE(!ecosim::param_set(bad, 0, 1001));  // the accessor refuses
  bad.light_permille = 1001;                     // bypass it, as a bad client would
  CHECK_EQ_i32(ECOSIM_ERR_INVALID, ecosim_params_set(w, &bad));
  CHECK_TRUE(std::strlen(ecosim_last_error()) > 0);
  EcosimParams after{};
  after.struct_size = sizeof(after);
  CHECK_EQ_i32(ECOSIM_OK, ecosim_params_get(w, &after));
  CHECK_EQ_i32(light, after.light_permille);

  ecosim_world_destroy(w);
}

ECOSIM_TEST(params_abi_struct_sizes_are_published) {
  CHECK_EQ_i32((int32_t)sizeof(EcosimParams), ecosim_abi_struct_size(ECOSIM_STRUCT_PARAMS));
  CHECK_EQ_i32((int32_t)sizeof(EcosimParamDesc),
               ecosim_abi_struct_size(ECOSIM_STRUCT_PARAM_DESC));
}

ECOSIM_TEST(changing_parameters_changes_the_ecosystem) {
  // Two identical worlds, one with the light off: the populations must diverge.
  EcosimWorld* a = eco_world();
  EcosimWorld* b = eco_world();

  EcosimSeedConfig seed{};
  seed.struct_size = sizeof(seed);
  seed.species = 2;
  seed.density_permille = 200;
  seed.seed = 2024;
  CHECK_EQ_i32(ECOSIM_OK, ecosim_randomize(a, &seed));
  CHECK_EQ_i32(ECOSIM_OK, ecosim_randomize(b, &seed));

  EcosimParams dark{};
  dark.struct_size = sizeof(dark);
  CHECK_EQ_i32(ECOSIM_OK, ecosim_params_get(b, &dark));
  dark.light_permille = 0;
  dark.fertility_permille = 0;
  CHECK_EQ_i32(ECOSIM_OK, ecosim_params_set(b, &dark));

  // Grazers starve within ~50 generations either way; what separates the two
  // worlds is what happens after: with light and fertility the carcass resource
  // keeps trickling up until producers sprout and persist, while the dark world
  // stays empty forever.
  CHECK_EQ_i32(ECOSIM_OK, ecosim_world_step(a, 1500));
  CHECK_EQ_i32(ECOSIM_OK, ecosim_world_step(b, 1500));

  EcosimStats sa{};
  sa.struct_size = sizeof(sa);
  EcosimStats sb{};
  sb.struct_size = sizeof(sb);
  CHECK_EQ_i32(ECOSIM_OK, ecosim_stats(a, &sa));
  CHECK_EQ_i32(ECOSIM_OK, ecosim_stats(b, &sb));
  CHECK_TRUE(sa.alive_count > 0);
  CHECK_EQ_i32(0, sb.alive_count);
  CHECK_TRUE(ecosim_hash(a) != ecosim_hash(b));

  ecosim_world_destroy(a);
  ecosim_world_destroy(b);
}

ECOSIM_TEST(indexed_parameter_access_matches_the_struct_path) {
  EcosimWorld* w = eco_world();
  const int32_t n = ecosim_param_count();
  for (int32_t i = 0; i < n; ++i) {
    EcosimParamDesc d{};
    d.struct_size = sizeof(d);
    CHECK_EQ_i32(ECOSIM_OK, ecosim_param_desc(i, &d));
    int32_t value = -1;
    CHECK_EQ_i32(ECOSIM_OK, ecosim_param_get(w, i, &value));
    CHECK_EQ_i32(d.default_value, value);

    const int32_t probe = d.min_value;
    CHECK_EQ_i32(ECOSIM_OK, ecosim_param_set(w, i, probe));
    CHECK_EQ_i32(ECOSIM_OK, ecosim_param_get(w, i, &value));
    CHECK_EQ_i32(probe, value);

    // The struct view must agree with the indexed view.
    EcosimParams all{};
    all.struct_size = sizeof(all);
    CHECK_EQ_i32(ECOSIM_OK, ecosim_params_get(w, &all));
    CHECK_EQ_i32(probe, ecosim::param_get(all, i));

    CHECK_EQ_i32(ECOSIM_ERR_INVALID, ecosim_param_set(w, i, d.max_value + 1));
  }
  int32_t dummy = 0;
  CHECK_EQ_i32(ECOSIM_ERR_RANGE, ecosim_param_get(w, n + 5, &dummy));
  CHECK_EQ_i32(ECOSIM_ERR_INVALID, ecosim_param_get(w, 0, nullptr));
  ecosim_world_destroy(w);
}
