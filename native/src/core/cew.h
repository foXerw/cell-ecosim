#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ecosim_abi.h"

namespace ecosim {

class World;

namespace cew {

/// Cell Ecosim World: the versioned binary container for a whole simulation
/// state - dimensions, generation, parameters and the three stored planes.
///
/// Unlike RLE (single-state by design, appendix A.6) this carries species,
/// energy and resource, so an ecosystem can be saved and read back behaving
/// the same. Age is not stored, matching the snapshot ring's trade-off.
struct Header {
  char magic[4];             // "CEWS"
  uint16_t version;          // container version this file was written with
  uint16_t header_size;      // so a reader can skip fields it does not know
  uint32_t w;
  uint32_t h;
  uint64_t generation;
  uint32_t body_size;        // params + three encoded planes
  uint8_t plane_modes[3];    // codec::Mode per plane
  uint8_t reserved[9];       // zeroed; future flags go here, not in new fields
};

static_assert(sizeof(Header) == 40, "cew header layout is part of the file format");

constexpr uint16_t kVersion = 1;
constexpr uint32_t kMaxSupportedVersion = kVersion;

/// Upper bound on the bytes encode() may produce, for callers that must
/// allocate before asking.
int32_t worst_case_size(int32_t w, int32_t h);

int32_t encode(const World& world, std::vector<uint8_t>& out);

/// Loads into `world`, replacing its cells, generation and parameters.
/// `why` receives a Chinese, user-presentable reason on failure.
int32_t decode(const uint8_t* data, size_t size, World& world, std::string& why);

}  // namespace cew
}  // namespace ecosim
