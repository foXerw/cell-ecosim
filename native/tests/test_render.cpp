#include "check.h"

#include <cstring>
#include <vector>

#include "ecosim_abi.h"

namespace {

EcosimWorld* make_world() {
  EcosimConfig cfg{};
  cfg.struct_size = sizeof(EcosimConfig);
  cfg.w = 2;
  cfg.h = 2;
  cfg.mode = ECOSIM_MODE_BIRSURV;
  cfg.topology = ECOSIM_TOPOLOGY_BOUNDED;
  cfg.threads = 1;
  return ecosim_world_create(&cfg);
}

EcosimPalette make_palette() {
  EcosimPalette pal{};
  pal.struct_size = sizeof(EcosimPalette);
  pal.cell_bgra[0] = 0xFF000000u;
  pal.cell_bgra[1] = 0xFF112233u;
  pal.cell_bgra[2] = 0xFF445566u;
  pal.cell_bgra[7] = 0xFFABCDEFu;
  return pal;
}

void expect_pixel(const uint8_t* buf, int32_t index, uint32_t bgra) {
  const uint8_t* p = buf + static_cast<size_t>(index) * 4;
  CHECK_EQ_i32(static_cast<int32_t>(bgra & 0xFF), p[0]);
  CHECK_EQ_i32(static_cast<int32_t>((bgra >> 8) & 0xFF), p[1]);
  CHECK_EQ_i32(static_cast<int32_t>((bgra >> 16) & 0xFF), p[2]);
  CHECK_EQ_i32(static_cast<int32_t>((bgra >> 24) & 0xFF), p[3]);
}

}  // namespace

ECOSIM_TEST(render_writes_bgra_per_species_in_row_major_order) {
  EcosimWorld* w = make_world();
  EcosimPaintOp op{};
  op.struct_size = sizeof(op);
  op.x0 = 1;
  op.y0 = 0;
  op.x1 = 2;
  op.y1 = 1;
  op.species = 1;
  CHECK_EQ_i32(ECOSIM_OK, ecosim_paint(w, &op, 1));
  op.x0 = 0;
  op.y0 = 1;
  op.x1 = 1;
  op.y1 = 2;
  op.species = 2;
  CHECK_EQ_i32(ECOSIM_OK, ecosim_paint(w, &op, 1));

  const EcosimPalette pal = make_palette();

  // Exact pitch: two rows of sixteen bytes. (Sizing this to 16 once wrote row
  // one past the end of the vector and poisoned every later assertion.)
  std::vector<uint8_t> buf(32, 0xEE);
  CHECK_EQ_i32(ECOSIM_OK, ecosim_render_frame(w, buf.data(), 16, &pal));
  expect_pixel(buf.data(), 0, 0xFF000000u);   // (0,0)
  expect_pixel(buf.data(), 1, 0xFF112233u);   // (1,0)
  expect_pixel(buf.data(), 4, 0xFF445566u);   // (0,1): row starts at byte 16
  expect_pixel(buf.data(), 5, 0xFF000000u);   // (1,1)

  // Padded pitch: row 1 must start at byte 24, and the padding must stay
  // untouched so a bitmap's stride survives.
  std::vector<uint8_t> padded(48, 0xEE);
  CHECK_EQ_i32(ECOSIM_OK, ecosim_render_frame(w, padded.data(), 24, &pal));
  expect_pixel(padded.data() + 24, 0, 0xFF445566u);
  expect_pixel(padded.data() + 24, 1, 0xFF000000u);
  CHECK_EQ_i32(0xEE, padded[32]);
  CHECK_EQ_i32(0xEE, padded[47]);

  ecosim_world_destroy(w);
}

ECOSIM_TEST(render_uses_the_slot_of_whatever_species_is_there) {
  EcosimWorld* w = make_world();
  EcosimPaintOp op{};
  op.struct_size = sizeof(op);
  op.x0 = 0;
  op.y0 = 0;
  op.x1 = 2;
  op.y1 = 2;
  op.species = 7;
  CHECK_EQ_i32(ECOSIM_OK, ecosim_paint(w, &op, 1));

  const EcosimPalette pal = make_palette();
  std::vector<uint8_t> buf(32, 0);
  CHECK_EQ_i32(ECOSIM_OK, ecosim_render_frame(w, buf.data(), 16, &pal));
  expect_pixel(buf.data(), 0, 0xFFABCDEFu);
  expect_pixel(buf.data(), 5, 0xFFABCDEFu);
  ecosim_world_destroy(w);
}

ECOSIM_TEST(render_rejects_bad_arguments) {
  EcosimWorld* w = make_world();
  const EcosimPalette pal = make_palette();
  std::vector<uint8_t> buf(16, 0);

  CHECK_EQ_i32(ECOSIM_ERR_INVALID, ecosim_render_frame(w, nullptr, 16, &pal));
  CHECK_EQ_i32(ECOSIM_ERR_INVALID, ecosim_render_frame(w, buf.data(), 16, nullptr));
  CHECK_EQ_i32(ECOSIM_ERR_INVALID, ecosim_render_frame(w, buf.data(), 4, &pal));  // < w*4

  EcosimPalette small{};
  small.struct_size = 8;
  CHECK_EQ_i32(ECOSIM_ERR_SIZE, ecosim_render_frame(w, buf.data(), 16, &small));

  CHECK_EQ_i32(ECOSIM_ERR_INVALID, ecosim_render_frame(nullptr, buf.data(), 16, &pal));
  ecosim_world_destroy(w);
}
