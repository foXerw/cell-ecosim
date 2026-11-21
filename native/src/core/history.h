#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "ecosim_abi.h"

namespace ecosim {

/// Per-species population census for one generation. Filled by the pass that
/// already touches every cell (the B/S kernel, or the pipeline's regrow stage),
/// so keeping history costs no extra scan of the board.
using SpeciesCounts = std::array<int32_t, ECOSIM_MAX_SPECIES>;

inline void merge_counts(SpeciesCounts& into, const SpeciesCounts& added) {
  for (size_t i = 0; i < into.size(); ++i) into[i] += added[i];
}

inline int32_t total_alive(const SpeciesCounts& counts) {
  int32_t n = 0;
  for (size_t i = 1; i < counts.size(); ++i) n += counts[i];
  return n;
}

/// Sliding window of censuses, oldest dropped. Fixed capacity, allocated once
/// with the world: stepping must never allocate.
class HistoryRing {
 public:
  struct Sample {
    uint64_t generation;
    int32_t alive;
    SpeciesCounts counts;
  };

  explicit HistoryRing(int32_t capacity);

  void push(uint64_t generation, const SpeciesCounts& counts);
  void clear();

  int32_t size() const { return size_; }
  int32_t capacity() const { return static_cast<int32_t>(samples_.size()); }

  /// 0 = oldest kept, size()-1 = newest.
  const Sample& at(int32_t index) const;

 private:
  std::vector<Sample> samples_;
  int32_t head_ = 0;  // next write slot
  int32_t size_ = 0;
};

}  // namespace ecosim
