#include "core/parallel.h"

#include <algorithm>
#include <thread>

namespace ecosim {
namespace parallel {

namespace {

constexpr int32_t kMinBandRows = 64;
constexpr int32_t kMaxThreads = 8;

}  // namespace

int32_t default_threads() {
  unsigned hw = std::thread::hardware_concurrency();
  if (hw == 0) hw = 1;
  return std::clamp(static_cast<int32_t>(hw), 1, kMaxThreads);
}

int32_t band_rows_for(int32_t grid_height, int32_t threads) {
  if (grid_height <= 0) return 1;
  const int32_t t = std::max(threads, 1);
  // Several bands per thread so a slow band cannot stall the whole generation.
  const int32_t target = (grid_height + t * 4 - 1) / (t * 4);
  int32_t band = std::max(kMinBandRows, ((target + kMinBandRows - 1) / kMinBandRows) * kMinBandRows);
  return std::min(band, grid_height);
}

Pool::Pool(int32_t n_threads) {
  if (n_threads > 1) {
    const int32_t want = std::min(n_threads, kMaxThreads + 1) - 1;  // the caller is one
    workers_.reserve(static_cast<size_t>(want));
    for (int32_t i = 0; i < want; ++i) {
      workers_.emplace_back([this](std::stop_token st) { worker_loop(st); });
    }
  }
}

Pool::~Pool() {
  // jthread joins on destruction, so waiters must be woken first. Changing the
  // stop state *inside* the lock is what makes the wake-up reliable: a worker
  // that has already evaluated the predicate but not yet parked would otherwise
  // miss the notification and sleep forever.
  {
    std::lock_guard<std::mutex> lk(mutex_);
    for (auto& t : workers_) t.request_stop();
  }
  work_ready_.notify_all();
}

void Pool::worker_loop(std::stop_token st) {
  uint64_t seen = 0;
  while (!st.stop_requested()) {
    std::unique_lock<std::mutex> lk(mutex_);
    work_ready_.wait(lk, [&] {
      return st.stop_requested() || generation_.load(std::memory_order_acquire) != seen;
    });
    if (st.stop_requested()) return;
    seen = generation_.load(std::memory_order_acquire);
    lk.unlock();
    execute_bands();
  }
}

void Pool::execute_bands() {
  const std::function<void(int32_t, int32_t)>* body;
  int32_t total_rows;
  int32_t band_rows;
  {
    // Snapshot under the lock: the caller's std::function is only guaranteed
    // alive until every participant has exited (see run_bands' wait).
    std::lock_guard<std::mutex> lk(mutex_);
    body = body_;
    total_rows = total_rows_;
    band_rows = band_rows_;
  }

  entered_.fetch_add(1, std::memory_order_acq_rel);

  while (true) {
    const int32_t index = next_band_.fetch_add(1, std::memory_order_relaxed);
    const int32_t y0 = index * band_rows;
    if (y0 >= total_rows) break;
    const int32_t y1 = std::min(total_rows, y0 + band_rows);
    (*body)(y0, y1);
    bands_left_.fetch_sub(1, std::memory_order_acq_rel);
  }

  if (exited_.fetch_add(1, std::memory_order_acq_rel) + 1 ==
      expected_.load(std::memory_order_acquire)) {
    std::lock_guard<std::mutex> lk(mutex_);
    all_done_.notify_all();
  }
}

void Pool::run_bands(int32_t total_rows, int32_t band_rows,
                     const std::function<void(int32_t, int32_t)>& body) {
  if (total_rows <= 0 || band_rows <= 0) return;

  if (workers_.empty()) {
    // Single-threaded: run inline, no handoff, no atomics. This is the
    // baseline the equivalence tests compare the parallel path against.
    for (int32_t y0 = 0; y0 < total_rows; y0 += band_rows) {
      body(y0, std::min(total_rows, y0 + band_rows));
    }
    return;
  }

  const int32_t bands = (total_rows + band_rows - 1) / band_rows;
  {
    std::lock_guard<std::mutex> lk(mutex_);
    body_ = &body;
    total_rows_ = total_rows;
    band_rows_ = band_rows;
    expected_.store(static_cast<int32_t>(workers_.size()) + 1, std::memory_order_relaxed);
    next_band_.store(0, std::memory_order_relaxed);
    bands_left_.store(bands, std::memory_order_relaxed);
    entered_.store(0, std::memory_order_relaxed);
    exited_.store(0, std::memory_order_relaxed);
    generation_.fetch_add(1, std::memory_order_release);
  }
  work_ready_.notify_all();

  execute_bands();

  std::unique_lock<std::mutex> lk(mutex_);
  all_done_.wait(lk, [&] {
    return exited_.load(std::memory_order_acquire) ==
               expected_.load(std::memory_order_acquire) &&
           bands_left_.load(std::memory_order_acquire) == 0;
  });
  body_ = nullptr;
}

}  // namespace parallel
}  // namespace ecosim
