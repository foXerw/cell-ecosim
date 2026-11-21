#include "core/snapshots.h"

#include <algorithm>
#include <cstring>

#include "core/plane_codec.h"
#include "core/species.h"
#include "util/error.h"

namespace ecosim {

namespace {

constexpr uint8_t kModeRaw = codec::kModeRaw;
constexpr uint8_t kModeRle = codec::kModeRle;

struct Header {
  uint8_t version;
  uint8_t mode_species;
  uint8_t mode_energy;
  uint8_t mode_resource;
  uint8_t pad;
  uint32_t cells;
};

}  // namespace

SnapshotRing::SnapshotRing(int32_t slots) : slots_(static_cast<size_t>(std::max(1, slots))) {}

void SnapshotRing::clear() {
  for (auto& slot : slots_) slot.blob.clear();
  head_ = 0;
  size_ = 0;
  captured_ = 0;
  stride_ = kInitialStride;
  frames_at_stride_ = 0;
  next_due_ = 0;
}

int32_t SnapshotRing::capture(uint64_t generation, const Grid& grid) {
  const Planes& cur = grid.cur();

  // The planes are encoded into `body` with their modes in locals, then the
  // header is prepended. Handing the output buffer to encode_plane while also
  // writing the mode into that same buffer would leave a reference into a
  // vector that its own append is about to reallocate.
  std::vector<uint8_t> body;
  body.reserve(static_cast<size_t>(grid.count()));
  uint8_t mode_species = kModeRaw;
  uint8_t mode_energy = kModeRaw;
  uint8_t mode_resource = kModeRaw;
  codec::encode_plane(cur.species, body, mode_species);
  codec::encode_plane(cur.energy, body, mode_energy);
  codec::encode_plane(cur.resource, body, mode_resource);

  Header header{};
  header.version = 1;
  header.mode_species = mode_species;
  header.mode_energy = mode_energy;
  header.mode_resource = mode_resource;
  header.cells = static_cast<uint32_t>(grid.count());

  std::vector<uint8_t> blob;
  blob.reserve(sizeof(header) + body.size());
  codec::append_bytes(blob, &header, sizeof(header));
  blob.insert(blob.end(), body.begin(), body.end());

  Slot& slot = slots_[static_cast<size_t>(head_)];
  slot.generation = generation;
  slot.blob = std::move(blob);

  head_ = (head_ + 1) % static_cast<int32_t>(slots_.size());
  if (size_ < static_cast<int32_t>(slots_.size())) ++size_;

  ++captured_;
  return size_ - 1;
}

void SnapshotRing::maybe_capture(uint64_t generation, const Grid& grid) {
  const uint64_t cells = grid.count();
  const uint64_t scale = std::max<uint64_t>(1, cells / kStrideCellBudget);
  const uint64_t effective_stride = static_cast<uint64_t>(stride_) * scale;
  if (effective_stride == 0 || generation % effective_stride != 0) return;

  capture(generation, grid);
  if (++frames_at_stride_ >= static_cast<int32_t>(slots_.size()) / 2) {
    frames_at_stride_ = 0;
    stride_ = std::min(stride_ * 2, 4096);
  }
}

uint64_t SnapshotRing::generation_at(int32_t index) const {
  const int32_t capacity = static_cast<int32_t>(slots_.size());
  const int32_t oldest = (head_ - size_ + capacity) % capacity;
  const int32_t clamped = std::clamp(index, 0, size_ - 1);
  return slots_[static_cast<size_t>((oldest + clamped) % capacity)].generation;
}

int32_t SnapshotRing::restore(Grid& grid, int32_t index) const {
  if (size_ == 0) {
    set_error("没有可恢复的快照");
    return ECOSIM_ERR_STATE;
  }
  if (index < 0 || index >= size_) {
    char msg[96];
    std::snprintf(msg, sizeof(msg), "快照序号 %d 超出 0..%d", index, size_ - 1);
    set_error(msg);
    return ECOSIM_ERR_RANGE;
  }

  const int32_t capacity = static_cast<int32_t>(slots_.size());
  const int32_t oldest = (head_ - size_ + capacity) % capacity;
  const Slot& slot = slots_[static_cast<size_t>((oldest + index) % capacity)];

  Header header{};
  std::memcpy(&header, slot.blob.data(), sizeof(header));
  if (header.cells != static_cast<uint32_t>(grid.count())) {
    char msg[128];
    std::snprintf(msg, sizeof(msg), "快照是 %u 格的世界，当前网格 %dx%d 不匹配", header.cells,
                  grid.w(), grid.h());
    set_error(msg);
    return ECOSIM_ERR_SIZE;
  }

  Planes& cur = grid.cur();
  size_t cursor = sizeof(header);
  bool ok = codec::decode_plane(slot.blob.data(), slot.blob.size(), cursor, header.mode_species,
                         cur.species) &&
            codec::decode_plane(slot.blob.data(), slot.blob.size(), cursor, header.mode_energy,
                         cur.energy) &&
            codec::decode_plane(slot.blob.data(), slot.blob.size(), cursor, header.mode_resource,
                         cur.resource);
  if (!ok) {
    set_error("快照数据损坏（解码越界）");
    return ECOSIM_ERR_INVALID;
  }

  // Age is not stored; energy is, so a restored cell keeps exactly what it had.
  std::fill(cur.age.begin(), cur.age.end(), uint16_t{0});
  grid.copy_cur_to_nxt();
  return ECOSIM_OK;
}

size_t SnapshotRing::bytes_used() const {
  size_t total = 0;
  for (const auto& slot : slots_) total += slot.blob.size();
  return total;
}

}  // namespace ecosim
