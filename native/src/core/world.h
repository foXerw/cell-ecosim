#pragma once

#include <cstdint>
#include <memory>

#include "core/grid.h"
#include "core/kernel.h"
#include "core/parallel.h"
#include "ecosim_abi.h"

namespace ecosim {

// The whole simulation state of one world. Deliberately free of UI concepts,
// file paths and threading decisions beyond `threads` (design §7).
class World {
 public:
  // Throws std::invalid_argument on a rejected config; the ABI layer turns that
  // into a nullptr plus a diagnostic, so no exception ever crosses the border.
  explicit World(const EcosimConfig& cfg);

  int32_t step(int32_t n_steps);
  int32_t set_topology(int32_t topology);

  /// Writes half-open rectangles into the current buffer. Validated as a whole
  /// before anything is written, so one bad op cannot half-apply a stroke.
  /// The next buffer is resynchronised afterwards: painting while paused must
  /// not be overwritten by the first step that follows.
  int32_t paint(const EcosimPaintOp* ops, int32_t count);

  uint64_t generation() const { return generation_; }
  const EcosimConfig& config() const { return cfg_; }
  int32_t topology() const { return cfg_.topology; }
  int32_t threads() const { return cfg_.threads; }

  Grid& grid() { return grid_; }
  const Grid& grid() const { return grid_; }
  const RuleTable& rules() const { return rules_; }

  static bool validate(const EcosimConfig& cfg, char* why, int32_t cap);

 private:
  EcosimConfig cfg_;
  RuleTable rules_;
  Grid grid_;
  std::unique_ptr<parallel::Pool> pool_;
  uint64_t generation_ = 0;
};

}  // namespace ecosim
