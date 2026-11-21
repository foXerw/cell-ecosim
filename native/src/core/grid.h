#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ecosim {

// State of one generation, laid out as four parallel planes rather than one
// array of cell structs (SoA). The hot loop reads `species` only, so it streams
// four bytes per cell of cache instead of dragging energy/age along.
struct Planes {
  std::vector<uint8_t> species;
  std::vector<uint8_t> resource;
  std::vector<uint16_t> energy;
  std::vector<uint16_t> age;
};

// Ping-pong buffer pair: rules read `cur` and write `nxt`, then swap.
class Grid {
 public:
  int32_t w() const { return w_; }
  int32_t h() const { return h_; }
  size_t count() const { return static_cast<size_t>(w_) * static_cast<size_t>(h_); }
  size_t index(int32_t x, int32_t y) const {
    return static_cast<size_t>(y) * static_cast<size_t>(w_) + static_cast<size_t>(x);
  }

  // Allocates and zeroes both buffer sets. Called once at world creation:
  // there is deliberately no resize (design appendix A.4).
  void allocate(int32_t w, int32_t h);

  Planes& cur() { return cur_; }
  Planes& nxt() { return nxt_; }
  const Planes& cur() const { return cur_; }
  const Planes& nxt() const { return nxt_; }

  /// Third buffer for pipeline stages that must not read their own output
  /// (reproduce and disperse). Scratch only; never swapped into cur.
  Planes& aux() { return aux_; }

  // Swaps the two buffer sets. Cheap: only vector headers move, no memcpy.
  void swap_buffers();

  // Mirrors cur into nxt. Placeholder for the evolution kernel (milestone M1
  // task 4 replaces this call with a real rule pass).
  void copy_cur_to_nxt();

 private:
  int32_t w_ = 0;
  int32_t h_ = 0;
  Planes cur_;
  Planes nxt_;
  Planes aux_;
};

}  // namespace ecosim
