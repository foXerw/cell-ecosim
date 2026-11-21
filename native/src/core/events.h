#pragma once

#include <cstdint>
#include <vector>

#include "ecosim_abi.h"

namespace ecosim {

/// What the engine noticed about its own population history.
///
/// Detection lives here rather than in the UI: "this species just went extinct"
/// and "this species just outbreak-ed" are facts about generations of state, and
/// the managed side only pulls what happened. Pull, not callback - a native
/// thread calling into managed code would need thread affinity the app does not
/// have (design appendix A.1).
class EventQueue {
 public:
  explicit EventQueue(int32_t capacity = 256);

  /// Call once per generation with the fresh census and the number of cells.
  void observe(uint64_t generation, const int32_t* counts, int32_t cells);

  /// Takes everything queued (the queue is empty afterwards), oldest first.
  int32_t poll(EcosimEvent* out, int32_t cap);

  int32_t pending() const { return static_cast<int32_t>(queue_.size()); }
  void clear();

 private:
  struct State {
    int32_t previous = 0;
    bool outbreak_armed = true;
  };

  std::vector<EcosimEvent> queue_;
  std::vector<State> states_;
  int32_t capacity_;
};

}  // namespace ecosim
