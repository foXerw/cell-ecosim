#pragma once

#include <cstdint>
#include <memory>

#include "core/grid.h"
#include "core/kernel.h"
#include "core/parallel.h"
#include "core/params.h"
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

  /// Writes one BGRA8 pixel per cell into the caller's buffer, `row_pitch`
  /// bytes per row. The caller owns the memory; this never allocates.
  int32_t render_frame(uint8_t* bgra, int32_t row_pitch, const EcosimPalette* palette) const;

  /// Fills the grid with one species (0 = empty) and zeroes age/energy/
  /// resource. The generation counter is deliberately left alone: clearing the
  /// board is not rewinding time.
  int32_t clear(int32_t fill_species);

  /// Stateless per-cell draw (util/rng.h): the same seed and generation give
  /// the same grid no matter how the work is split across threads.
  int32_t randomize(const EcosimSeedConfig& seed_cfg);

  int32_t stats(EcosimStats& out) const;

  const EcosimParams& params() const { return params_; }
  int32_t set_params(const EcosimParams& p);

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
  EcosimParams params_;
  Grid grid_;
  std::unique_ptr<parallel::Pool> pool_;
  uint64_t generation_ = 0;
};

}  // namespace ecosim
