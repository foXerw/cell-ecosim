#include "check.h"

#include <cstring>
#include <string>
#include <vector>

#include "core/cew.h"
#include "core/params.h"
#include "core/world.h"
#include "ecosim_abi.h"

namespace {

EcosimConfig cfg_for(int32_t w, int32_t h) {
  EcosimConfig cfg{};
  cfg.struct_size = sizeof(cfg);
  cfg.w = w;
  cfg.h = h;
  cfg.mode = ECOSIM_MODE_ECOSYSTEM;
  cfg.topology = ECOSIM_TOPOLOGY_TORUS;
  cfg.threads = 1;
  cfg.rng_seed = 31;
  return cfg;
}

void seed_soup(ecosim::World& w) {
  EcosimSeedConfig seed{};
  seed.struct_size = sizeof(seed);
  seed.species = 1;
  seed.density_permille = 300;
  seed.seed = 8;
  CHECK_EQ_i32(ECOSIM_OK, w.randomize(seed));
}

}  // namespace

ECOSIM_TEST(cew_roundtrip_restores_cells_generation_and_params) {
  ecosim::World source(cfg_for(20, 20));
  seed_soup(source);
  EcosimParams params = ecosim::default_params();
  params.light_permille = 123;
  params.fertility_permille = 45;
  params.repro_scale_permille[2] = 777;
  CHECK_EQ_i32(ECOSIM_OK, source.set_params(params));
  source.step(12);

  const auto before = source.grid().cur();
  std::vector<uint8_t> file;
  CHECK_EQ_i32(ECOSIM_OK, ecosim::cew::encode(source, file));

  ecosim::World target(cfg_for(20, 20));
  seed_soup(target);
  target.step(3);
  std::string why;
  CHECK_EQ_i32(ECOSIM_OK, ecosim::cew::decode(file.data(), file.size(), target, why));

  const auto& after = target.grid().cur();
  CHECK_TRUE(before.species == after.species);
  CHECK_TRUE(before.energy == after.energy);
  CHECK_TRUE(before.resource == after.resource);
  CHECK_EQ_i64(12, static_cast<int64_t>(target.generation()));
  CHECK_EQ_i32(123, target.params().light_permille);
  CHECK_EQ_i32(45, target.params().fertility_permille);
  CHECK_EQ_i32(777, target.params().repro_scale_permille[2]);
}

ECOSIM_TEST(cew_encoding_is_byte_stable) {
  ecosim::World w(cfg_for(16, 16));
  seed_soup(w);
  w.step(5);

  std::vector<uint8_t> first;
  std::vector<uint8_t> second;
  CHECK_EQ_i32(ECOSIM_OK, ecosim::cew::encode(w, first));
  CHECK_EQ_i32(ECOSIM_OK, ecosim::cew::encode(w, second));
  CHECK_TRUE(first == second);
  CHECK_TRUE(first.size() <= static_cast<size_t>(
      ecosim::cew::worst_case_size(16, 16)));
}

ECOSIM_TEST(a_truncated_file_is_corrupt_not_empty) {
  ecosim::World source(cfg_for(16, 16));
  seed_soup(source);
  source.step(4);
  std::vector<uint8_t> file;
  CHECK_EQ_i32(ECOSIM_OK, ecosim::cew::encode(source, file));
  file.resize(file.size() / 2);

  ecosim::World target(cfg_for(16, 16));
  std::string why;
  CHECK_EQ_i32(ECOSIM_ERR_INVALID, ecosim::cew::decode(file.data(), file.size(), target, why));
  CHECK_TRUE(why.find("损坏") != std::string::npos);
}

ECOSIM_TEST(wrong_magic_and_a_future_version_say_different_things) {
  ecosim::World source(cfg_for(8, 8));
  seed_soup(source);
  std::vector<uint8_t> file;
  CHECK_EQ_i32(ECOSIM_OK, ecosim::cew::encode(source, file));

  ecosim::World target(cfg_for(8, 8));
  std::string why;

  std::vector<uint8_t> not_cew = file;
  not_cew[0] = 'X';
  CHECK_EQ_i32(ECOSIM_ERR_INVALID, ecosim::cew::decode(not_cew.data(), not_cew.size(), target, why));
  CHECK_TRUE(why.find("CEWS") != std::string::npos);

  std::vector<uint8_t> future = file;
  future[4] = 99;  // version low byte
  CHECK_EQ_i32(ECOSIM_ERR_INVALID, ecosim::cew::decode(future.data(), future.size(), target, why));
  CHECK_TRUE(why.find("版本") != std::string::npos);

  CHECK_EQ_i32(ECOSIM_ERR_INVALID, ecosim::cew::decode(nullptr, 100, target, why));
  CHECK_EQ_i32(ECOSIM_ERR_INVALID, ecosim::cew::decode(file.data(), 8, target, why));
}

ECOSIM_TEST(a_size_mismatch_names_both_boards) {
  ecosim::World source(cfg_for(8, 8));
  seed_soup(source);
  std::vector<uint8_t> file;
  CHECK_EQ_i32(ECOSIM_OK, ecosim::cew::encode(source, file));

  ecosim::World small(cfg_for(4, 4));
  std::string why;
  CHECK_EQ_i32(ECOSIM_ERR_RANGE, ecosim::cew::decode(file.data(), file.size(), small, why));
  CHECK_TRUE(why.find("64") != std::string::npos);
  CHECK_TRUE(why.find("16") != std::string::npos);
  CHECK_TRUE(why.find("尺寸不符") != std::string::npos);
}

ECOSIM_TEST(the_abi_saves_and_loads_through_buffers) {
  EcosimWorld* source = ecosim_world_create(&cfg_for(12, 12));
  EcosimSeedConfig seed{};
  seed.struct_size = sizeof(seed);
  seed.species = 2;
  seed.density_permille = 250;
  seed.seed = 6;
  CHECK_EQ_i32(ECOSIM_OK, ecosim_randomize(source, &seed));
  CHECK_EQ_i32(ECOSIM_OK, ecosim_world_step(source, 7));

  int32_t needed = 0;
  CHECK_EQ_i32(ECOSIM_OK, ecosim_cew_size(source, &needed));
  CHECK_TRUE(needed > 0);

  std::vector<uint8_t> buffer(static_cast<size_t>(needed));
  int32_t written = 0;
  CHECK_EQ_i32(ECOSIM_OK, ecosim_save_cew(source, buffer.data(), needed, &written));
  CHECK_TRUE(written > 0 && written <= needed);

  // A buffer one byte too small is refused with the number it needed.
  int32_t ignored = 0;
  CHECK_EQ_i32(ECOSIM_ERR_SIZE,
               ecosim_save_cew(source, buffer.data(), written - 1, &ignored));
  CHECK_TRUE(std::strlen(ecosim_last_error()) > 0);

  EcosimWorld* target = ecosim_world_create(&cfg_for(12, 12));
  CHECK_EQ_i32(ECOSIM_OK, ecosim_load_cew(target, buffer.data(), written));
  EcosimStats a{};
  a.struct_size = sizeof(a);
  EcosimStats b{};
  b.struct_size = sizeof(b);
  CHECK_EQ_i32(ECOSIM_OK, ecosim_stats(source, &a));
  CHECK_EQ_i32(ECOSIM_OK, ecosim_stats(target, &b));
  CHECK_EQ_i32(a.alive_count, b.alive_count);
  CHECK_EQ_i64(static_cast<int64_t>(ecosim_hash(source)), static_cast<int64_t>(ecosim_hash(target)));

  CHECK_EQ_i32(ECOSIM_ERR_INVALID, ecosim_load_cew(target, nullptr, 10));
  CHECK_EQ_i32(ECOSIM_ERR_INVALID, ecosim_load_cew(target, buffer.data(), 0));

  ecosim_world_destroy(source);
  ecosim_world_destroy(target);
}
