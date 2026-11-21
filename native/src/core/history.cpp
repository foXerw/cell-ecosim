#include "core/history.h"

#include <algorithm>

namespace ecosim {

HistoryRing::HistoryRing(int32_t capacity) : samples_(std::max(capacity, 1)) {}

void HistoryRing::push(uint64_t generation, const SpeciesCounts& counts) {
  Sample& slot = samples_[static_cast<size_t>(head_)];
  slot.generation = generation;
  slot.counts = counts;
  slot.alive = total_alive(counts);

  head_ = (head_ + 1) % capacity();
  if (size_ < capacity()) ++size_;
}

void HistoryRing::clear() {
  head_ = 0;
  size_ = 0;
}

const HistoryRing::Sample& HistoryRing::at(int32_t index) const {
  // Oldest first: the ring's write head is one past the newest, and the oldest
  // kept sample sits `size_` slots behind it.
  const int32_t capacity = this->capacity();
  int32_t oldest = (head_ - size_ + capacity) % capacity;
  const int32_t clamped = std::clamp(index, 0, size_ - 1);
  return samples_[static_cast<size_t>((oldest + clamped) % capacity)];
}

}  // namespace ecosim
