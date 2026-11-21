#include "core/cew.h"

#include <cstdio>
#include <cstring>

#include "core/plane_codec.h"
#include "core/world.h"

namespace ecosim {
namespace cew {

int32_t worst_case_size(int32_t w, int32_t h) {
  const int64_t cells = static_cast<int64_t>(w) * h;
  // header + params + three planes at their worst (value + 4-byte count per
  // cell) + slack
  const int64_t bytes = static_cast<int64_t>(sizeof(Header)) +
                        static_cast<int64_t>(sizeof(EcosimParams)) + cells * 7 + 64;
  return static_cast<int32_t>(bytes > 0x7fffffff ? 0x7fffffff : bytes);
}

int32_t encode(const World& world, std::vector<uint8_t>& out) {
  const Grid& grid = world.grid();
  const Planes& cur = grid.cur();

  std::vector<uint8_t> planes;
  planes.reserve(static_cast<size_t>(grid.count()));
  uint8_t mode_species = codec::kModeRaw;
  uint8_t mode_energy = codec::kModeRaw;
  uint8_t mode_resource = codec::kModeRaw;
  codec::encode_plane(cur.species, planes, mode_species);
  codec::encode_plane(cur.energy, planes, mode_energy);
  codec::encode_plane(cur.resource, planes, mode_resource);

  Header header{};
  // A fixed four-byte tag, not a string. Treating it as one truncated the
  // signature to three letters, and every reader then correctly called the
  // archive "not a Cell Ecosim file".
  std::memcpy(header.magic, "CEWS", sizeof(header.magic));
  header.version = kVersion;
  header.header_size = sizeof(Header);
  header.w = static_cast<uint32_t>(grid.w());
  header.h = static_cast<uint32_t>(grid.h());
  header.generation = world.generation();
  header.body_size = static_cast<uint32_t>(sizeof(EcosimParams) + planes.size());
  header.plane_modes[0] = mode_species;
  header.plane_modes[1] = mode_energy;
  header.plane_modes[2] = mode_resource;

  out.clear();
  out.reserve(sizeof(header) + sizeof(EcosimParams) + planes.size());
  codec::append_bytes(out, &header, sizeof(header));
  codec::append_bytes(out, &world.params(), sizeof(EcosimParams));
  out.insert(out.end(), planes.begin(), planes.end());
  return ECOSIM_OK;
}

int32_t decode(const uint8_t* data, size_t size, World& world, std::string& why) {
  if (data == nullptr || size < sizeof(Header)) {
    why = "存档太短，不像一个 .cew 文件（可能已损坏）";
    return ECOSIM_ERR_INVALID;
  }

  Header header{};
  std::memcpy(&header, data, sizeof(header));
  if (std::memcmp(header.magic, "CEWS", sizeof(header.magic)) != 0) {
    why = "不是 Cell Ecosim 存档（缺少 CEWS 魔数）";
    return ECOSIM_ERR_INVALID;
  }
  if (header.version == 0 || header.version > kMaxSupportedVersion) {
    char msg[128];
    std::snprintf(msg, sizeof(msg), "存档版本 v%u 无法读取（本引擎支持到 v%u）",
                  static_cast<unsigned>(header.version),
                  static_cast<unsigned>(kMaxSupportedVersion));
    why = msg;
    return ECOSIM_ERR_INVALID;
  }
  if (header.header_size > size) {
    why = "存档头部长度超出文件长度（已损坏）";
    return ECOSIM_ERR_INVALID;
  }

  const uint64_t cells = static_cast<uint64_t>(header.w) * header.h;
  if (cells != world.grid().count()) {
    char msg[192];
    std::snprintf(msg, sizeof(msg), "存档是 %ux%u（%llu 格），当前网格 %dx%d（%zu 格），尺寸不符",
                  header.w, header.h, static_cast<unsigned long long>(cells), world.grid().w(),
                  world.grid().h(), world.grid().count());
    why = msg;
    return ECOSIM_ERR_RANGE;
  }

  size_t cursor = header.header_size;
  EcosimParams params{};
  if (cursor + sizeof(EcosimParams) > size) {
    why = "存档缺少参数块（已损坏）";
    return ECOSIM_ERR_INVALID;
  }
  std::memcpy(&params, data + cursor, sizeof(EcosimParams));
  cursor += sizeof(EcosimParams);
  if (params.struct_size < static_cast<int32_t>(sizeof(EcosimParams))) {
    why = "存档里的参数块比本引擎的还旧，无法安全读取";
    return ECOSIM_ERR_SIZE;
  }

  Planes& cur = world.grid().cur();
  const uint8_t* body = data + cursor;
  const size_t remaining = size - cursor;
  size_t inner = 0;
  const bool ok =
      codec::decode_plane(body, remaining, inner, header.plane_modes[0], cur.species) &&
      codec::decode_plane(body, remaining, inner, header.plane_modes[1], cur.energy) &&
      codec::decode_plane(body, remaining, inner, header.plane_modes[2], cur.resource);
  if (!ok) {
    why = "存档数据损坏：平面在声明长度之前就结束了";
    return ECOSIM_ERR_INVALID;
  }

  std::fill(cur.age.begin(), cur.age.end(), uint16_t{0});
  world.set_params(params);
  world.set_generation(header.generation);
  world.grid().copy_cur_to_nxt();
  world.recount();
  why.clear();
  return ECOSIM_OK;
}

}  // namespace cew
}  // namespace ecosim
