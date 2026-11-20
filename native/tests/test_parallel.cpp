#include "check.h"

#include <atomic>
#include <cstring>
#include <thread>
#include <vector>

#include "core/parallel.h"
#include "ecosim_abi.h"

namespace {

void fill_random_ops(std::vector<EcosimPaintOp>& ops, int32_t w, int32_t h, uint64_t seed,
                     int32_t density_permille) {
  uint64_t s = seed;
  for (int32_t y = 0; y < h; ++y) {
    for (int32_t x = 0; x < w; ++x) {
      s = s * 6364136223846793005ull + 1442695040888963407ull;
      if ((static_cast<uint32_t>(s >> 32) % 1000u) < static_cast<uint32_t>(density_permille)) {
        EcosimPaintOp op{};
        op.struct_size = sizeof(EcosimPaintOp);
        op.x0 = x;
        op.y0 = y;
        op.x1 = x + 1;
        op.y1 = y + 1;
        op.species = 1;
        ops.push_back(op);
      }
    }
  }
}

struct WorldHandle {
  EcosimConfig cfg{};
  EcosimWorld* h = nullptr;

  explicit WorldHandle(int32_t threads) {
    cfg.struct_size = sizeof(EcosimConfig);
    cfg.w = 512;
    cfg.h = 512;
    cfg.mode = ECOSIM_MODE_BIRSURV;
    cfg.topology = ECOSIM_TOPOLOGY_TORUS;
    cfg.threads = threads;
    cfg.birth_mask[3] = 1;
    cfg.survive_mask[2] = 1;
    cfg.survive_mask[3] = 1;
    cfg.rng_seed = 999;
    h = ecosim_world_create(&cfg);
  }

  ~WorldHandle() { ecosim_world_destroy(h); }

  void paint(const std::vector<EcosimPaintOp>& ops) {
    CHECK_EQ_i32(ECOSIM_OK, ecosim_paint(h, ops.data(), static_cast<int32_t>(ops.size())));
  }

  std::vector<uint8_t> species() const {
    EcosimGridView view{};
    view.struct_size = sizeof(EcosimGridView);
    CHECK_EQ_i32(ECOSIM_OK, ecosim_world_grid_view(h, &view));
    std::vector<uint8_t> copy(static_cast<size_t>(view.w) * view.h);
    std::memcpy(copy.data(), view.species, copy.size());
    return copy;
  }
};

}  // namespace

ECOSIM_TEST(parallel_equals_serial_512_seed_999_threads_1_2_4_8) {
  std::vector<EcosimPaintOp> ops;
  fill_random_ops(ops, 512, 512, 999, 300);

  WorldHandle baseline(1);
  baseline.paint(ops);
  CHECK_EQ_i32(ECOSIM_OK, ecosim_world_step(baseline.h, 15));
  const std::vector<uint8_t> want = baseline.species();

  for (const int32_t threads : {2, 4, 8}) {
    WorldHandle w(threads);
    w.paint(ops);
    CHECK_EQ_i32(ECOSIM_OK, ecosim_world_step(w.h, 15));
    const std::vector<uint8_t> got = w.species();
    CHECK_TRUE(got == want);  // band splitting must not change a single cell

    int32_t diffs = 0;
    for (size_t i = 0; i < want.size(); ++i) diffs += want[i] != got[i];
    CHECK_EQ_i32(0, diffs);
  }
}

ECOSIM_TEST(band_plan_covers_every_row_exactly_once) {
  constexpr int32_t kRows = 100;
  std::vector<std::atomic<int>> hits(kRows);
  for (auto& a : hits) a.store(0);

  ecosim::parallel::Pool pool(4);
  pool.run_bands(kRows, 8, [&](int32_t y0, int32_t y1) {
    for (int32_t y = y0; y < y1; ++y) hits[static_cast<size_t>(y)].fetch_add(1);
  });

  for (int32_t y = 0; y < kRows; ++y) CHECK_EQ_i32(1, hits[static_cast<size_t>(y)].load());
}

ECOSIM_TEST(bands_never_overlap_the_written_range) {
  // Two threads writing the same row would show up as a doubled marker.
  std::vector<std::atomic<int>> marks(256);
  for (auto& m : marks) m.store(0);
  ecosim::parallel::Pool pool(6);
  for (int32_t round = 0; round < 20; ++round) {
    pool.run_bands(256, 16, [&](int32_t y0, int32_t y1) {
      for (int32_t y = y0; y < y1; ++y) marks[static_cast<size_t>(y)].fetch_add(1);
    });
  }
  for (int32_t y = 0; y < 256; ++y) CHECK_EQ_i32(20, marks[static_cast<size_t>(y)].load());
}

ECOSIM_TEST(pool_with_one_thread_runs_inline) {
  ecosim::parallel::Pool pool(1);
  CHECK_EQ_i32(1, pool.size());
  const std::thread::id caller = std::this_thread::get_id();
  std::thread::id seen{};
  pool.run_bands(64, 16, [&](int32_t, int32_t) { seen = std::this_thread::get_id(); });
  CHECK_TRUE(seen == caller);
}

ECOSIM_TEST(band_rows_are_at_least_64_and_never_exceed_the_grid) {
  using ecosim::parallel::band_rows_for;
  CHECK_EQ_i32(64, band_rows_for(64, 4));
  CHECK_EQ_i32(64, band_rows_for(100, 4));
  CHECK_EQ_i32(1024, band_rows_for(4096, 1));  // four bands for one thread
  CHECK_TRUE(band_rows_for(20, 8) <= 20);
  CHECK_TRUE(band_rows_for(2048, 8) >= 64);
  // Several bands per thread, otherwise one slow band stalls a generation.
  CHECK_TRUE(2048 / band_rows_for(2048, 8) >= 8);
}
