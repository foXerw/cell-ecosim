#include "core/kernel.h"

namespace ecosim {

RuleTable RuleTable::life() {
  RuleTable r;
  r.birth[3] = 1;
  r.survive[2] = 1;
  r.survive[3] = 1;
  return r;
}

RuleTable RuleTable::from_config(const EcosimConfig& cfg) {
  RuleTable r;
  for (size_t i = 0; i < 9; ++i) {
    r.birth[i] = cfg.birth_mask[i] ? 1 : 0;
    r.survive[i] = cfg.survive_mask[i] ? 1 : 0;
  }
  return r;
}

namespace kernel {

void step_birsurv(const Planes& in, Planes& out, int32_t w, int32_t h, const RuleTable& rules,
                  int32_t topology) {
  const bool torus = topology == ECOSIM_TOPOLOGY_TORUS;

  for (int32_t y = 0; y < h; ++y) {
    for (int32_t x = 0; x < w; ++x) {
      const size_t self = static_cast<size_t>(y) * w + x;
      const uint8_t self_species = in.species[self];

      int32_t live = 0;
      for (int32_t dy = -1; dy <= 1; ++dy) {
        for (int32_t dx = -1; dx <= 1; ++dx) {
          if (dx == 0 && dy == 0) continue;
          int32_t sx = x + dx;
          int32_t sy = y + dy;
          if (torus) {
            sx = (sx + w) % w;
            sy = (sy + h) % h;
          } else if (sx < 0 || sy < 0 || sx >= w || sy >= h) {
            continue;  // a bounded edge is permanent death, not a wall to wrap
          }
          live += in.species[static_cast<size_t>(sy) * w + sx] != 0;
        }
      }

      if (self_species != 0) {
        out.species[self] = rules.survive[live] ? self_species : 0;
      } else {
        out.species[self] = rules.birth[live] ? 1 : 0;
      }

      // Age counts consecutive generations of life; a dead cell resets to 0.
      out.age[self] = out.species[self]
                          ? static_cast<uint16_t>(self_species ? in.age[self] + 1 : 0)
                          : 0;

      // Energy and resource are untouched until the ecosystem pipeline (M2),
      // but they still have to travel to the other buffer, because swap_buffers
      // exchanges all four planes at once.
      out.energy[self] = in.energy[self];
      out.resource[self] = in.resource[self];
    }
  }
}

int32_t count_alive(const Planes& planes) {
  int32_t n = 0;
  for (const uint8_t s : planes.species) n += s != 0;
  return n;
}

}  // namespace kernel
}  // namespace ecosim
