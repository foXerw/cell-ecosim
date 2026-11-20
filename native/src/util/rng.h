#pragma once

#include <cstdint>

namespace ecosim {
namespace rng {

/// splitmix64 finaliser, used as a hash rather than a stream.
inline uint64_t splitmix64(uint64_t x) {
  x += 0x9E3779B97F4A7C15ull;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
  return x ^ (x >> 31);
}

/// Stateless draw for one (seed, generation, cell) triple.
///
/// Deliberately not a sequence: a cell's random value must not depend on how
/// the work was split across threads or on the order bands ran, otherwise the
/// "threads do not change the result" property (design appendix A.5.2) dies.
inline uint64_t draw(uint64_t seed, uint64_t generation, uint64_t index) {
  const uint64_t mixed = seed ^ (generation * 0xC2B2AE3D27D4EB4Full) ^
                         ((index + 1) * 0x9E3779B97F4A7C15ull);
  return splitmix64(mixed);
}

}  // namespace rng
}  // namespace ecosim
