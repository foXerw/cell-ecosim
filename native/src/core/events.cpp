#include "core/events.h"

#include <algorithm>
#include <cstring>

namespace ecosim {

namespace {

// An outbreak is > 5% of the board; it re-arms only below 4%. Without the gap
// a population hovering at the threshold would emit an event every generation
// and bury the log.
constexpr double kOutbreakShare = 0.05;
constexpr double kOutbreakResetShare = 0.04;

}  // namespace

EventQueue::EventQueue(int32_t capacity) : capacity_(std::max(1, capacity)) {}

void EventQueue::clear() {
  queue_.clear();
  states_.clear();
}

void EventQueue::observe(uint64_t generation, const int32_t* counts, int32_t cells) {
  if (counts == nullptr || cells <= 0) return;
  if (static_cast<int32_t>(states_.size()) < ECOSIM_MAX_SPECIES) {
    states_.resize(ECOSIM_MAX_SPECIES);
  }

  const int32_t outbreak_level = static_cast<int32_t>(cells * kOutbreakShare);
  const int32_t reset_level = static_cast<int32_t>(cells * kOutbreakResetShare);

  for (int32_t s = 1; s < ECOSIM_MAX_SPECIES; ++s) {
    const int32_t now = counts[s];
    State& state = states_[s];
    const int32_t before = state.previous;

    if (now == 0 && before > 0) {
      queue_.push_back(EcosimEvent{sizeof(EcosimEvent), generation, ECOSIM_EVENT_EXTINCTION, s, now});
    } else if (before == 0 && now > 0) {
      queue_.push_back(EcosimEvent{sizeof(EcosimEvent), generation, ECOSIM_EVENT_APPEARANCE, s, now});
    }

    if (state.outbreak_armed && now > outbreak_level && outbreak_level > 0) {
      queue_.push_back(EcosimEvent{sizeof(EcosimEvent), generation, ECOSIM_EVENT_OUTBREAK, s, now});
      state.outbreak_armed = false;
    } else if (!state.outbreak_armed && now < reset_level) {
      state.outbreak_armed = true;
    }

    state.previous = now;
  }

  // Overflow drops the oldest: a long run should not grow memory, and the
  // newest events are the ones a reader still cares about.
  const int32_t overflow = static_cast<int32_t>(queue_.size()) - capacity_;
  if (overflow > 0) {
    queue_.erase(queue_.begin(), queue_.begin() + overflow);
  }
}

int32_t EventQueue::poll(EcosimEvent* out, int32_t cap) {
  if (out == nullptr || cap <= 0) return 0;
  const int32_t taken = std::min(cap, static_cast<int32_t>(queue_.size()));
  std::memcpy(out, queue_.data(), sizeof(EcosimEvent) * static_cast<size_t>(taken));
  queue_.erase(queue_.begin(), queue_.begin() + taken);
  return taken;
}

}  // namespace ecosim
