#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

namespace ecosim {
namespace codec {

enum Mode : uint8_t {
  kModeRaw = 0,
  kModeRle = 1,
};

inline void append_bytes(std::vector<uint8_t>& out, const void* data, size_t size) {
  const size_t at = out.size();
  out.resize(at + size);
  std::memcpy(out.data() + at, data, size);
}

/// Run-length encodes one plane as (value, count) records. A board made of
/// single-cell runs would encode *larger* than raw, so the caller keeps
/// whichever is smaller and records the choice in the mode byte.
template <typename T>
void encode_plane(const std::vector<T>& plane, std::vector<uint8_t>& out, uint8_t& mode) {
  std::vector<uint8_t> rle;
  size_t i = 0;
  while (i < plane.size()) {
    const T value = plane[i];
    size_t run = 1;
    while (i + run < plane.size() && plane[i + run] == value) ++run;
    append_bytes(rle, &value, sizeof(value));
    const uint32_t count = static_cast<uint32_t>(run);
    append_bytes(rle, &count, sizeof(count));
    i += run;
  }

  if (rle.size() < plane.size() * sizeof(T)) {
    mode = kModeRle;
    out.insert(out.end(), rle.begin(), rle.end());
  } else {
    mode = kModeRaw;
    append_bytes(out, plane.data(), plane.size() * sizeof(T));
  }
}

template <typename T>
bool decode_plane(const uint8_t* data, size_t size, size_t& cursor, uint8_t mode,
                  std::vector<T>& plane) {
  if (mode == kModeRaw) {
    const size_t bytes = plane.size() * sizeof(T);
    if (cursor + bytes > size) return false;
    std::memcpy(plane.data(), data + cursor, bytes);
    cursor += bytes;
    return true;
  }

  if (mode != kModeRle) return false;
  size_t filled = 0;
  while (filled < plane.size()) {
    if (cursor + sizeof(T) + sizeof(uint32_t) > size) return false;
    T value{};
    uint32_t run = 0;
    std::memcpy(&value, data + cursor, sizeof(T));
    cursor += sizeof(T);
    std::memcpy(&run, data + cursor, sizeof(uint32_t));
    cursor += sizeof(uint32_t);
    if (run == 0 || filled + run > plane.size()) return false;
    std::fill_n(plane.begin() + static_cast<long>(filled), run, value);
    filled += run;
  }
  return true;
}

}  // namespace codec
}  // namespace ecosim
