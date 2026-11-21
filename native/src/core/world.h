#pragma once

#include <cstdint>
#include <memory>

#include "core/grid.h"
#include "core/history.h"
#include "core/kernel.h"
#include "core/parallel.h"
#include "core/events.h"
#include "core/params.h"
#include "core/snapshots.h"
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
  /// Bulk-loads a species plane. Used when switching rule modes, where the
  /// living cells must move across losslessly (RLE cannot do it: it is
  /// single-state by design, appendix A.6). Energy resets to each species'
  /// initial value and age to zero, per appendix A.3.
  int32_t load_species(const uint8_t* species, int32_t count);

  int32_t clear(int32_t fill_species);

  /// Stateless per-cell draw (util/rng.h): the same seed and generation give
  /// the same grid no matter how the work is split across threads.
  int32_t randomize(const EcosimSeedConfig& seed_cfg);

  int32_t stats(EcosimStats& out) const;

  /// Inline on purpose: the pipeline reports a census, it does not call into
  /// world.cpp, and an archive cannot resolve a cycle between two members.
  void record_generation(const SpeciesCounts& counts) { history_.push(generation_ + 1, counts); }

  const HistoryRing& history() const { return history_; }

  EventQueue& events() { return events_; }

  SnapshotRing& snapshots() { return snapshots_; }

  /// Restores a snapshot and rewinds the generation counter to it, so stepping
  /// forward afterwards reproduces exactly what the engine produced the first
  /// time - which is what makes the scrubber honest.
  int32_t restore_snapshot(int32_t index) {
    const int32_t rc = snapshots_.restore(grid_, index);
    if (rc == ECOSIM_OK) generation_ = snapshots_.generation_at(index);
    return rc;
  }

  const EcosimParams& params() const { return params_; }
  int32_t set_params(const EcosimParams& p);

  /// One tunable at a time, addressed by the description table's index.
  int32_t get_param(int32_t index, int32_t* out_value) const;
  int32_t set_param(int32_t index, int32_t value);

  uint64_t generation() const { return generation_; }

  /// Only .cew loading rewinds time; the pipeline never calls this.
  void set_generation(uint64_t generation) { generation_ = generation; }
  const EcosimConfig& config() const { return cfg_; }
  int32_t topology() const { return cfg_.topology; }
  int32_t threads() const { return cfg_.threads; }

  Grid& grid() { return grid_; }
  const Grid& grid() const { return grid_; }
  const RuleTable& rules() const { return rules_; }

  /// The band workers, shared by the B/S kernel and the ecosystem pipeline.
  parallel::Pool& pool() { return *pool_; }

  static bool validate(const EcosimConfig& cfg, char* why, int32_t cap);

 private:
  EcosimConfig cfg_;
  RuleTable rules_;
  EcosimParams params_;
  Grid grid_;
  HistoryRing history_;
  EventQueue events_;
  SnapshotRing snapshots_;
  std::unique_ptr<parallel::Pool> pool_;
  uint64_t generation_ = 0;
};

}  // namespace ecosim
