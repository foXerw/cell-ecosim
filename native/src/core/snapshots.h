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
  static constexpr int32_t kDenseFrames = 16;

  explicit SnapshotRing(int32_t slots = kDefaultSlots);

  /// Captures on the decimation schedule: the first kDenseFrames generations
  /// one by one, then every 2nd, every 4th, ... so a long run stays inside the
  /// ring without a fixed memory cost per frame.
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
  int32_t stride_ = 1;
  int32_t frames_at_stride_ = 0;
  uint64_t next_due_ = 0;
};

}  // namespace ecosim
