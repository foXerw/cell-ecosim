#pragma once

#include <cstdint>
#include <vector>

#include "core/grid.h"
#include "ecosim_abi.h"

namespace ecosim {

/// The engine's own review history, and the single store for it: the managed
/// timeline is a view over these numbers, not a second copy of the board
/// (design appendix A.3).
///
/// What is kept is species + energy + resource, run-length compressed when that
/// actually helps. Age is not stored - it is derived, and losing it is what
/// keeps 64 frames affordable. Restoring therefore leaves every living cell at
/// age 0.
class SnapshotRing {
 public:
  static constexpr int32_t kDefaultSlots = 64;

  /// Capturing means walking three planes, and at 2048x2048 that costs more
  /// than a generation of simulation. Measured: capturing every generation took
  /// Life at 1024x1024 from 0.59 to 2.10 ms/gen. So the ring samples every
  /// kInitialStride generations and widens from there - a review timeline that
  /// steps 8 generations at a time is still a review timeline.
  static constexpr int32_t kInitialStride = 8;

  /// A capture walks three planes, so its cost scales with the board. Sampling
  /// every 8 generations is right for 512x512 and ruinous for 2048x2048 - it
  /// pushed the ecosystem below its 20 FPS gate. The stride therefore grows
  /// with board size so the amortised cost per generation stays bounded,
  /// and review granularity coarsens on huge boards rather than the frame rate.
  static constexpr uint64_t kStrideCellBudget = 262144;  // 512x512

  explicit SnapshotRing(int32_t slots = kDefaultSlots);

  /// Captures on the decimation schedule: every kInitialStride generations at
  /// first, then the stride doubles as the ring fills, so a long run stays
  /// inside the ring without taxing every generation.
  void maybe_capture(uint64_t generation, const Grid& grid);

  /// Manual capture (Ctrl+K). Returns the ring index of the new snapshot, or
  /// ECOSIM_ERR_* on a bad grid.
  int32_t capture(uint64_t generation, const Grid& grid);

  int32_t count() const { return size_; }
  uint64_t generation_at(int32_t index) const;

  /// index 0 = oldest kept. Writes into grid.cur and mirrors it into grid.nxt.
  int32_t restore(Grid& grid, int32_t index) const;

  void clear();

  /// Total bytes held by stored snapshots, so the memory claim in the design
  /// stays checked rather than merely asserted.
  size_t bytes_used() const;

 private:
  struct Slot {
    uint64_t generation = 0;
    std::vector<uint8_t> blob;
  };

  std::vector<Slot> slots_;
  int32_t head_ = 0;
  int32_t size_ = 0;
  int32_t captured_ = 0;
  int32_t stride_ = kInitialStride;
  int32_t frames_at_stride_ = 0;
  uint64_t next_due_ = 0;
};

}  // namespace ecosim
