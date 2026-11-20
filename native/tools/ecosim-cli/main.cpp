// Headless consumer of the same C ABI the desktop app binds to. If a feature
// cannot be driven from here, it is not really in the engine yet.
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/parallel.h"
#include "ecosim_abi.h"

namespace {

constexpr int kExitOk = 0;
constexpr int kExitArgs = 2;
constexpr int kExitNative = 3;

struct Options {
  int32_t w = 64;
  int32_t h = 64;
  int32_t gens = 0;
  int32_t threads = 0;
  int32_t density = 300;
  uint64_t seed = 1;
  std::string rule = "life";
  std::string birth = "3";
  std::string survive = "23";
  std::string topology = "torus";
  std::string rle_file;
  std::string out_file;
};

int fail_args(const char* message) {
  std::fprintf(stderr, "参数错误：%s\n", message);
  std::fprintf(stderr,
               "用法：\n"
               "  ecosim-cli run   --w N --h N --gens N [--rule life|highlife|custom]\n"
               "                   [--birth 3] [--survive 23] [--topology torus|bounded]\n"
               "                   [--seed N] [--density PERMILLE] [--rle FILE] [--out FILE]\n"
               "                   [--threads N]\n"
               "  ecosim-cli bench --w N --h N --gens N [--threads N] [--seed N]\n"
               "                   [--density PERMILLE] [--rle FILE]\n"
               "  ecosim-cli hash  --rle FILE [--w N --h N] --gens N\n"
               "退出码：0 成功，2 参数错误，3 引擎失败\n");
  return kExitArgs;
}

bool parse_mask(const std::string& text, uint8_t* mask) {
  std::memset(mask, 0, 9);
  for (const char c : text) {
    if (c < '0' || c > '8') return false;
    mask[c - '0'] = 1;
  }
  return true;
}

bool read_file(const std::string& path, std::string& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::ostringstream ss;
  ss << in.rdbuf();
  out = ss.str();
  return true;
}

// The engine has no randomize yet (milestone M1 task 10), so the CLI seeds its
// own soup with a fixed LCG. Deterministic per seed, which is what bench needs.
void fill_soup(EcosimWorld* world, const Options& o) {
  std::vector<EcosimPaintOp> ops;
  uint64_t s = o.seed;
  for (int32_t y = 0; y < o.h; ++y) {
    for (int32_t x = 0; x < o.w; ++x) {
      s = s * 6364136223846793005ull + 1442695040888963407ull;
      if ((static_cast<uint32_t>(s >> 32) % 1000u) < static_cast<uint32_t>(o.density)) {
        EcosimPaintOp op{};
        op.struct_size = sizeof(op);
        op.x0 = x;
        op.y0 = y;
        op.x1 = x + 1;
        op.y1 = y + 1;
        op.species = 1;
        ops.push_back(op);
      }
    }
  }
  if (!ops.empty()) ecosim_paint(world, ops.data(), static_cast<int32_t>(ops.size()));
}

EcosimWorld* create_world(const Options& o, std::string& error) {
  EcosimConfig cfg{};
  cfg.struct_size = sizeof(EcosimConfig);
  cfg.w = o.w;
  cfg.h = o.h;
  cfg.mode = ECOSIM_MODE_BIRSURV;
  cfg.topology = o.topology == "bounded" ? ECOSIM_TOPOLOGY_BOUNDED : ECOSIM_TOPOLOGY_TORUS;
  cfg.threads = o.threads;
  cfg.rng_seed = o.seed;

  uint8_t birth[9];
  uint8_t survive[9];
  if (o.rule == "life") {
    parse_mask("3", birth);
    parse_mask("23", survive);
  } else if (o.rule == "highlife") {
    parse_mask("36", birth);
    parse_mask("23", survive);
  } else if (o.rule == "custom") {
    if (!parse_mask(o.birth, birth) || !parse_mask(o.survive, survive)) {
      error = "--birth/--survive 只能是 0..8 的数字串";
      return nullptr;
    }
  } else {
    error = "未知规则：" + o.rule;
    return nullptr;
  }
  std::memcpy(cfg.birth_mask, birth, 9);
  std::memcpy(cfg.survive_mask, survive, 9);

  EcosimWorld* world = ecosim_world_create(&cfg);
  if (world == nullptr) error = ecosim_last_error();
  return world;
}

int load_input(EcosimWorld* world, const Options& o) {
  if (o.rle_file.empty()) {
    fill_soup(world, o);
    return kExitOk;
  }
  std::string text;
  if (!read_file(o.rle_file, text)) {
    std::fprintf(stderr, "无法读取 %s\n", o.rle_file.c_str());
    return kExitNative;
  }
  if (ecosim_from_rle(world, text.c_str()) != ECOSIM_OK) {
    std::fprintf(stderr, "RLE 解析失败：%s\n", ecosim_last_error());
    return kExitNative;
  }
  return kExitOk;
}

bool parse_options(int argc, char** argv, int start, Options& o, std::string& error) {
  for (int i = start; i < argc; ++i) {
    const std::string key = argv[i];
    auto value = [&](const char* what) -> const char* {
      if (i + 1 >= argc) {
        error = std::string(what) + " 需要一个值";
        return nullptr;
      }
      return argv[++i];
    };
    auto as_int = [&](const char* what, int32_t& target) -> bool {
      const char* v = value(what);
      if (!v) return false;
      char* end = nullptr;
      const long parsed = std::strtol(v, &end, 10);
      if (end == v || *end != '\0') {
        error = std::string(what) + " 需要整数，收到 " + v;
        return false;
      }
      target = static_cast<int32_t>(parsed);
      return true;
    };

    if (key == "--w") { if (!as_int("--w", o.w)) return false; }
    else if (key == "--h") { if (!as_int("--h", o.h)) return false; }
    else if (key == "--gens") { if (!as_int("--gens", o.gens)) return false; }
    else if (key == "--threads") { if (!as_int("--threads", o.threads)) return false; }
    else if (key == "--density") { if (!as_int("--density", o.density)) return false; }
    else if (key == "--seed") {
      const char* v = value("--seed");
      if (!v) return false;
      o.seed = std::strtoull(v, nullptr, 10);
    }
    else if (key == "--rule") { const char* v = value("--rule"); if (!v) return false; o.rule = v; }
    else if (key == "--birth") { const char* v = value("--birth"); if (!v) return false; o.birth = v; }
    else if (key == "--survive") { const char* v = value("--survive"); if (!v) return false; o.survive = v; }
    else if (key == "--topology") { const char* v = value("--topology"); if (!v) return false; o.topology = v; }
    else if (key == "--rle") { const char* v = value("--rle"); if (!v) return false; o.rle_file = v; }
    else if (key == "--out") { const char* v = value("--out"); if (!v) return false; o.out_file = v; }
    else { error = "未知选项 " + key; return false; }
  }
  return true;
}

int write_rle(EcosimWorld* world, const std::string& path) {
  int32_t needed = 0;
  if (ecosim_rle_size(world, &needed) != ECOSIM_OK) {
    std::fprintf(stderr, "rle_size 失败：%s\n", ecosim_last_error());
    return kExitNative;
  }
  std::vector<char> buffer(static_cast<size_t>(needed));
  int32_t written = 0;
  if (ecosim_to_rle(world, buffer.data(), needed, &written) != ECOSIM_OK) {
    std::fprintf(stderr, "to_rle 失败：%s\n", ecosim_last_error());
    return kExitNative;
  }
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    std::fprintf(stderr, "无法写入 %s\n", path.c_str());
    return kExitNative;
  }
  out.write(buffer.data(), written - 1);
  return kExitOk;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return fail_args("缺少子命令");
  const std::string command = argv[1];

  Options o;
  std::string error;
  if (!parse_options(argc, argv, 2, o, error)) return fail_args(error.c_str());

  if (command != "run" && command != "bench" && command != "hash") {
    return fail_args(("未知子命令 " + command).c_str());
  }
  if (o.gens < 0) return fail_args("--gens 不能为负");
  if (o.w <= 0 || o.h <= 0) return fail_args("--w/--h 必须为正");

  EcosimWorld* world = create_world(o, error);
  if (world == nullptr) {
    std::fprintf(stderr, "引擎失败：%s\n", error.c_str());
    return kExitNative;
  }

  int rc = load_input(world, o);
  if (rc != kExitOk) {
    ecosim_world_destroy(world);
    return rc;
  }

  if (command == "run") {
    if (ecosim_world_step(world, o.gens) != ECOSIM_OK) {
      std::fprintf(stderr, "step 失败：%s\n", ecosim_last_error());
      ecosim_world_destroy(world);
      return kExitNative;
    }
    if (!o.out_file.empty()) rc = write_rle(world, o.out_file);
    std::printf("gens=%lld hash=%016llx\n",
                static_cast<long long>(ecosim_generation(world)),
                static_cast<unsigned long long>(ecosim_hash(world)));
  } else if (command == "bench") {
    const auto started = std::chrono::steady_clock::now();
    const int32_t step_rc = ecosim_world_step(world, o.gens);
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started);
    if (step_rc != ECOSIM_OK) {
      std::fprintf(stderr, "step 失败：%s\n", ecosim_last_error());
      ecosim_world_destroy(world);
      return kExitNative;
    }
    const double seconds = elapsed.count();
    const double ms_per_gen = o.gens > 0 ? seconds * 1000.0 / o.gens : 0.0;
    std::printf("gens=%d seconds=%.4f gens_per_sec=%.2f ms_per_gen=%.3f hash=%016llx\n", o.gens,
                seconds, o.gens > 0 ? o.gens / seconds : 0.0, ms_per_gen,
                static_cast<unsigned long long>(ecosim_hash(world)));
  } else {  // hash
    if (o.rle_file.empty()) {
      ecosim_world_destroy(world);
      return fail_args("hash 子命令需要 --rle");
    }
    if (ecosim_world_step(world, o.gens) != ECOSIM_OK) {
      std::fprintf(stderr, "step 失败：%s\n", ecosim_last_error());
      ecosim_world_destroy(world);
      return kExitNative;
    }
    std::printf("%016llx\n", static_cast<unsigned long long>(ecosim_hash(world)));
  }

  ecosim_world_destroy(world);
  return rc;
}
