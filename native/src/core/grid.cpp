#include "core/grid.h"

#include <algorithm>
#include <cstring>

namespace ecosim {

void Grid::allocate(int32_t w, int32_t h) {
  w_ = w;
  h_ = h;
  const size_t n = count();
  for (Planes* p : {&cur_, &nxt_}) {
    p->species.assign(n, 0);
    p->resource.assign(n, 0);
    p->energy.assign(n, 0);
    p->age.assign(n, 0);
  }
}

void Grid::swap_buffers() {
  std::swap(cur_.species, nxt_.species);
  std::swap(cur_.resource, nxt_.resource);
  std::swap(cur_.energy, nxt_.energy);
  std::swap(cur_.age, nxt_.age);
}

void Grid::copy_cur_to_nxt() {
  nxt_.species = cur_.species;
  nxt_.resource = cur_.resource;
  nxt_.energy = cur_.energy;
  nxt_.age = cur_.age;
}

}  // namespace ecosim
