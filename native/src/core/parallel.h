#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace ecosim {
namespace parallel {

/// A fixed set of row-band workers, created once with the world and reused for
/// every generation. Threads are never started inside step(): the cost of a
/// generation is the work, not the setup.
///
/// With one requested thread there are no workers at all and run_bands()
/// executes on the calling thread, which keeps the single-threaded path free of
/// any synchronisation and gives the tests a baseline to compare against.
class Pool {
 public:
  explicit Pool(int32_t n_threads);
  ~Pool();

  Pool(const Pool&) = delete;
  Pool& operator=(const Pool&) = delete;

  /// Number of threads that will execute bands, including the caller.
  int32_t size() const { return static_cast<int32_t>(workers_.size()) + 1; }

  /// Splits [0, total_rows) into bands of at most band_rows and runs `body`
  /// over them. Returns only after every band is finished, so the caller can
  /// swap buffers safely.
  void run_bands(int32_t total_rows, int32_t band_rows,
                 const std::function<void(int32_t y0, int32_t y1)>& body);

 private:
  void worker_loop(std::stop_token st);
  void execute_bands();

  std::vector<std::jthread> workers_;
  std::mutex mutex_;
  std::condition_variable work_ready_;
  std::condition_variable all_done_;

  // The band currently being executed. Only valid while generation_ matches.
  const std::function<void(int32_t, int32_t)>* body_ = nullptr;
  int32_t total_rows_ = 0;
  int32_t band_rows_ = 1;
  std::atomic<int32_t> next_band_{0};
  std::atomic<int32_t> bands_left_{0};
  std::atomic<uint64_t> generation_{0};

  // Barrier bookkeeping: the caller may not return (and destroy `body`) until
  // every participant has entered *and* left execute_bands().
  std::atomic<int32_t> entered_{0};
  std::atomic<int32_t> exited_{0};
  std::atomic<int32_t> expected_{1};
};

/// Band height for a grid: at least 64 rows, aligned to 64, and small enough
/// that every worker gets several bands to chew on.
int32_t band_rows_for(int32_t grid_height, int32_t threads);

/// Thread count for "let the engine decide" (EcosimConfig.threads == 0):
/// hardware concurrency, capped, because bands smaller than a few dozen rows
/// cost more in handoffs than they save.
int32_t default_threads();

}  // namespace parallel
}  // namespace ecosim
