#include "check.h"

#include <cstring>
#include <string>
#include <vector>

#include "ecosim_abi.h"

namespace {

EcosimConfig make_config(int32_t w, int32_t h) {
  EcosimConfig cfg{};
  cfg.struct_size = sizeof(EcosimConfig);
  cfg.w = w;
  cfg.h = h;
  cfg.mode = ECOSIM_MODE_BIRSURV;
  cfg.topology = ECOSIM_TOPOLOGY_TORUS;
  cfg.threads = 1;
  cfg.birth_mask[3] = 1;
  cfg.survive_mask[2] = 1;
  cfg.survive_mask[3] = 1;
  cfg.rng_seed = 42;
  return cfg;
}

std::string to_rle(EcosimWorld* w) {
  int32_t needed = 0;
  CHECK_EQ_i32(ECOSIM_OK, ecosim_rle_size(w, &needed));
  std::vector<char> buf(static_cast<size_t>(needed));
  int32_t written = 0;
  CHECK_EQ_i32(ECOSIM_OK, ecosim_to_rle(w, buf.data(), needed, &written));
  return std::string(buf.data());
}

int32_t alive_count(EcosimWorld* w) {
  EcosimGridView view{};
  view.struct_size = sizeof(EcosimGridView);
  CHECK_EQ_i32(ECOSIM_OK, ecosim_world_grid_view(w, &view));
  int32_t n = 0;
  for (size_t i = 0; i < static_cast<size_t>(view.w) * view.h; ++i) n += view.species[i] != 0;
  return n;
}

void paint_random(EcosimWorld* w, int32_t gw, int32_t gh, uint64_t seed, int32_t density) {
  std::vector<EcosimPaintOp> ops;
  uint64_t s = seed;
  for (int32_t y = 0; y < gh; ++y) {
    for (int32_t x = 0; x < gw; ++x) {
      s = s * 6364136223846793005ull + 1442695040888963407ull;
      if ((static_cast<uint32_t>(s >> 32) % 1000u) < static_cast<uint32_t>(density)) {
        EcosimPaintOp op{};
        op.struct_size = sizeof(op);
        op.x0 = x;
        op.y0 = y;
        op.x1 = x + 1;
        op.y1 = y + 1;
        op.species = 1;
        ops.push_back(op);
      }
    }
  }
  CHECK_EQ_i32(ECOSIM_OK, ecosim_paint(w, ops.data(), static_cast<int32_t>(ops.size())));
}

}  // namespace

ECOSIM_TEST(rle_roundtrip_512_random_seed_12345) {
  EcosimConfig cfg = make_config(512, 512);
  EcosimWorld* a = ecosim_world_create(&cfg);
  CHECK_TRUE(a != nullptr);
  paint_random(a, 512, 512, 12345, 300);
  const std::string text = to_rle(a);
  const uint64_t hash_before = ecosim_hash(a);

  EcosimWorld* b = ecosim_world_create(&cfg);
  CHECK_EQ_i32(ECOSIM_OK, ecosim_from_rle(b, text.c_str()));
  CHECK_EQ_i64(static_cast<int64_t>(hash_before), static_cast<int64_t>(ecosim_hash(b)));

  // and after both evolve, they must still agree
  CHECK_EQ_i32(ECOSIM_OK, ecosim_world_step(a, 7));
  CHECK_EQ_i32(ECOSIM_OK, ecosim_world_step(b, 7));
  CHECK_EQ_i64(static_cast<int64_t>(ecosim_hash(a)), static_cast<int64_t>(ecosim_hash(b)));

  ecosim_world_destroy(a);
  ecosim_world_destroy(b);
}

ECOSIM_TEST(rle_header_reports_bounding_box_and_rule) {
  EcosimConfig cfg = make_config(40, 40);
  EcosimWorld* w = ecosim_world_create(&cfg);
  const char* pulsar =
      "x = 13, y = 13, rule = B3/S23\n"
      "2b3o3b3o2b$13b$o3bo3bo3bo$o3bo3bo3bo$o3bo3bo3bo$2b3o3b3o2b$13b$"
      "2b3o3b3o2b$o3bo3bo3bo$o3bo3bo3bo$o3bo3bo3bo$13b$2b3o3b3o2b!\n";
  CHECK_EQ_i32(ECOSIM_OK, ecosim_from_rle(w, pulsar));
  CHECK_EQ_i32(48, alive_count(w));  // the pulsar has 48 cells

  const std::string text = to_rle(w);
  CHECK_TRUE(text.rfind("x = 13, y = 13", 0) == 0);
  CHECK_TRUE(text.find("rule = B3/S23") != std::string::npos);
  CHECK_TRUE(text.back() == '\n');
  CHECK_TRUE(text.find('!') != std::string::npos);
  ecosim_world_destroy(w);
}

ECOSIM_TEST(rle_rejects_garbage_and_overlong_run) {
  EcosimConfig cfg = make_config(8, 8);
  EcosimWorld* w = ecosim_world_create(&cfg);

  CHECK_TRUE(ecosim_from_rle(w, "10a!") < 0);
  CHECK_TRUE(std::strlen(ecosim_last_error()) > 0);

  CHECK_TRUE(ecosim_from_rle(w, "99999999999999999b!") < 0);
  CHECK_TRUE(ecosim_from_rle(w, "3o$3o$3o") < 0);  // missing '!'
  CHECK_TRUE(ecosim_from_rle(w, "") < 0);
  CHECK_TRUE(ecosim_from_rle(w, nullptr) < 0);

  ecosim_world_destroy(w);
}

ECOSIM_TEST(rle_accepts_comments_spaces_and_wrapped_rows) {
  EcosimConfig cfg = make_config(8, 8);
  EcosimWorld* w = ecosim_world_create(&cfg);
  const char* messy = "#N glider with comments\n"
                      "#C written by hand\n"
                      "x = 3, y = 3, rule = B3/S23\n"
                      "bob $ 2bo $ 3o !\n";
  CHECK_EQ_i32(ECOSIM_OK, ecosim_from_rle(w, messy));
  CHECK_EQ_i32(5, alive_count(w));
  ecosim_world_destroy(w);
}

ECOSIM_TEST(rle_empty_world_encodes_as_one_by_one) {
  EcosimConfig cfg = make_config(6, 6);
  EcosimWorld* w = ecosim_world_create(&cfg);
  const std::string text = to_rle(w);
  CHECK_TRUE(text.rfind("x = 1, y = 1", 0) == 0);
  CHECK_EQ_i32(ECOSIM_OK, ecosim_from_rle(w, text.c_str()));
  CHECK_EQ_i32(0, alive_count(w));
  ecosim_world_destroy(w);
}

ECOSIM_TEST(rle_oversized_pattern_clips_with_a_warning_not_an_error) {
  EcosimConfig cfg = make_config(4, 4);
  EcosimWorld* w = ecosim_world_create(&cfg);
  const std::string wide(60, 'o');
  const std::string text = "x = 60, y = 1\n" + wide + "!";
  CHECK_EQ_i32(ECOSIM_OK, ecosim_from_rle(w, text.c_str()));
  // Read the diagnostic before anything else: every call clears it.
  const std::string diagnostic = ecosim_last_error();
  CHECK_EQ_i32(4, alive_count(w));
  CHECK_TRUE(diagnostic.find("裁剪") != std::string::npos);
  ecosim_world_destroy(w);
}

ECOSIM_TEST(rle_too_small_buffer_is_reported_as_size_error) {
  EcosimConfig cfg = make_config(32, 32);
  EcosimWorld* w = ecosim_world_create(&cfg);
  paint_random(w, 32, 32, 7, 400);
  int32_t needed = 0;
  CHECK_EQ_i32(ECOSIM_OK, ecosim_rle_size(w, &needed));
  CHECK_TRUE(needed > 0);

  // worst_case_size is a bound, not the exact length, so measure the real
  // output first and then hand back one byte less than that.
  std::vector<char> enough(static_cast<size_t>(needed));
  int32_t written = 0;
  CHECK_EQ_i32(ECOSIM_OK, ecosim_to_rle(w, enough.data(), needed, &written));
  CHECK_TRUE(written > 1);

  int32_t ignored = 0;
  CHECK_EQ_i32(ECOSIM_ERR_SIZE, ecosim_to_rle(w, enough.data(), written - 1, &ignored));
  CHECK_TRUE(std::strlen(ecosim_last_error()) > 0);
  ecosim_world_destroy(w);
}

ECOSIM_TEST(rle_at_places_the_pattern_at_the_requested_origin) {
  EcosimConfig cfg = make_config(8, 8);
  EcosimWorld* w = ecosim_world_create(&cfg);
  const char* glider = "x = 3, y = 3, rule = B3/S23\nbob$2bo$3o!\n";
  CHECK_EQ_i32(ECOSIM_OK, ecosim_from_rle_at(w, glider, 1, 1));

  EcosimGridView view{};
  view.struct_size = sizeof(EcosimGridView);
  CHECK_EQ_i32(ECOSIM_OK, ecosim_world_grid_view(w, &view));
  // The glider's own (1,0),(2,1),(0,2),(1,2),(2,2) plus the (1,1) offset.
  const int expect_x[5] = {2, 3, 1, 2, 3};
  const int expect_y[5] = {1, 2, 3, 3, 3};
  for (int i = 0; i < 5; ++i) {
    const size_t idx = static_cast<size_t>(expect_y[i]) * view.row_pitch_cells + expect_x[i];
    CHECK_EQ_i32(1, view.species[idx]);
  }
  CHECK_EQ_i32(5, alive_count(w));
  ecosim_world_destroy(w);
}
