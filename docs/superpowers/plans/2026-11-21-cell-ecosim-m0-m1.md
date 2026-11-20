# Cell Ecosim — M0 骨架 + M1 Life 内核 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 交付一个能真正玩起来的细胞自动机桌面应用：C++ 原生引擎（`ecosim.dll`，纯 C ABI）+ C# Avalonia 前端，M1 结束时 Conway 生命游戏完整可用（画、跑、缩放、图案库、RLE 导入导出、性能可测）。

**Architecture:** native 引擎拥有计算与内存布局（SoA 网格、popcount 邻域内核、行带并行），只通过 `native/include/ecosim_abi.h` 的 C ABI 暴露不透明句柄；C# 侧 `Ecosim.Core` 是唯一跨边界程序集（`SafeHandle` + 固定布局 DTO + 借用指针），`Ecosim.App` 只做 MVVM 呈现，C# 拥有时钟节奏。同一套 ABI 的第二个消费者是 headless `ecosim-cli`。

**Tech Stack:** C++20 (MinGW g++ 16.1)、CMake 4.4、C ABI DLL、Avalonia 12.1.6 (.NET 8)、xUnit、CTest、Git Bash 脚本。

**Spec:** `docs/superpowers/specs/2026-11-21-cell-ecosim-design.md`（本计划实现其 §5 约束、§6 方案 A、§7 架构、§8.1/8.2/8.3 数据模型的 Life 部分、§9 内核、§10 ABI、§11 C# 侧、§13 的 M0 与 M1；§8.4 六阶段生态管线、参数描述表、热力/回放/`cew` 属于 M2–M4，另立计划）

## Global Constraints

每个任务都隐含遵守以下条目，值逐字取自设计文档：

- C++ 标准：`-std=c++20`；本机只有 MinGW `g++` 16.1（ucrt64），**没有 MSVC、没有 clang**。
- native 产物静态链接运行时：`-static -static-libgcc -static-libstdc++`，`ecosim.dll` 不得依赖 MSYS2 运行时；输出文件名必须是 `ecosim.dll`（不是 `libecosim.dll`）。
- CMake generator：`scripts/configure.sh` 按机器上实际存在的构建工具自动选择（`ninja` > `MinGW Makefiles` > `MSYS Makefiles` > `Unix Makefiles`）。本机事实：**没有 mingw32-make、没有 ninja**（pacman 镜像不可达），只有 `/c/msys64/usr/bin/make.exe`，因此本机走 `MSYS Makefiles`（实施中的偏离，由 Claude 决定；原计划的 MinGW Makefiles 在其他机器上仍是首选）。configure 目录 `build/native`，产物 `build/native/ecosim.dll`、`ecosim-cli.exe`、`ecosim_tests.exe`（同一目录，测试才能找到 DLL）。
- C# 目标框架：`net8.0`；`Nullable: enable`、`ImplicitUsings: enable`、`LangVersion: latest`。Avalonia 包版本 `12.1.6`（`Avalonia`、`Avalonia.Desktop`、`Avalonia.Themes.Fluent`）。
- ABI：所有导出 `extern "C" __cdecl`，前缀 `ecosim_`；跨边界 `struct` 首字段一律 `int32_t struct_size`，一律 `#pragma pack(push,4)` 且 C# 侧 `[StructLayout(LayoutKind.Sequential, Pack = 4)]`；`enum` 一律 `int32_t`。
- 返回码：`ECOSIM_OK == 0`，负数为错误（`ECOSIM_ERR_INVALID=-1`、`ECOSIM_ERR_SIZE=-2`、`ECOSIM_ERR_STATE=-3`、`ECOSIM_ERR_RANGE=-4`）；错误详情走 `ecosim_last_error()`。
- 边界上禁止出现：C++ 对象、STL、异常、引用、`bool`、宽字符。
- 命名：C++ 文件 `snake_case`、类型 `PascalCase`、内部命名空间 `ecosim::`；C# 标准 .NET 命名，命名空间 `Ecosim.Core` / `Ecosim.App`。
- UI 可见文案用中文，代码标识符与注释用英文。
- Git：conventional commits，英文，小步提交（`chore:` / `feat:` / `test:` / `fix:` / `docs:`）。不提交 `build/`、`bin/`、`obj/`。
- 网格上限常量 `ECOSIM_MAX_DIM = 8192`；调色盘槽位常量 `ECOSIM_MAX_SPECIES = 16`。

## Review Focus

设计文档隐含、但下面每个任务的测试都没有直接覆盖、又最容易在使用中咬人的输入/状态，实施时按括号内的任务补钉住它的测试：

- **ABI 版本与 DLL 加载失败的用户可见体验**：`ecosim.dll` 缺失或 `ecosim_abi_version()` 不匹配时，窗口里应显示一句中文错误（"原生引擎版本不匹配：需要 vN，实际 vM"），而不是 `DllNotFoundException` 堆栈崩溃。（Task 2 的 `NativeLoadTests` + Task 11 的启动包装）
- **暂停状态下绘制的一致性**：暂停时画的格子必须出现在画布上，且下一步不被 `nxt` 缓冲覆盖掉（绘制写 `cur`，`step` 前把 `cur` 作为读源）。期望：暂停画 → 单步一次，刚画的图案按 Life 规则演化。（Task 10 的 `paint_then_step` 测试）
- **大网格 + 后台步进的 UI 响应性**：2048×2048 播放时 UI 线程不得阻塞（步进在 `SimClock` 后台，渲染只读已发布帧）。期望：拖动/缩放仍跟手，状态栏显示真实步进 ms。（Task 13 的 `LargeGridStaysResponsive` 测试 + Task 14 的实测清单）
- **RLE 坐标系与朝向**：从标准 Golly RLE 导入的滑翔机，方向和位置必须与 Life 社区约定一致（`b` = 活格，行优先，`y` 向下）。（Task 8 的 `glider.rle` 逐坐标测试）
- **跨借用语义的竞态**：`world_grid_view` / `render_frame` 读的是 native 内部缓冲，与后台 `step` 并发即数据竞争。期望：`World` 门面内一把 `Mutex` 串行化所有 native 调用，`TryGetFrame` 只在锁内写入 `FrameBuffer`。（Task 12 的 `ConcurrentPaintAndStepStaysConsistent` 测试）

---

## 最终 M1 ABI 面（跨任务共用契约，命名以此为准）

`native/include/ecosim_abi.h` 在 M1 结束时的完整导出清单：

```c
#define ECOSIM_ABI_VERSION 1
// 枚举
typedef enum EcosimMode      { ECOSIM_MODE_BIRSURV = 0, ECOSIM_MODE_ECOSYSTEM = 1 } EcosimMode;
typedef enum EcosimTopology  { ECOSIM_TOPOLOGY_TORUS = 0, ECOSIM_TOPOLOGY_BOUNDED = 1 } EcosimTopology;
typedef enum EcosimStructId  { ECOSIM_STRUCT_CONFIG = 1, ECOSIM_STRUCT_GRID_VIEW = 2,
                               ECOSIM_STRUCT_PALETTE = 3, ECOSIM_STRUCT_PAINT_OP = 4,
                               ECOSIM_STRUCT_STATS = 5, ECOSIM_STRUCT_SEED = 6 } EcosimStructId;

// 结构（全部首字段 struct_size，pack 4）
EcosimConfig     { int32 struct_size; int32 w; int32 h; int32 mode; int32 topology;
                   int32 threads; uint8 birth_mask[9]; uint8 survive_mask[9]; uint64 rng_seed; }
EcosimGridView   { int32 struct_size; int32 w; int32 h; int32 row_pitch_cells;
                   const uint8* species; const uint8* resource;
                   const uint16* energy; const uint16* age; }
EcosimPalette    { int32 struct_size; uint32 cell_bgra[ECOSIM_MAX_SPECIES]; }   // 下标 = species
EcosimPaintOp    { int32 struct_size; int32 x0; int32 y0; int32 x1; int32 y1; int32 species; }  // 半开区间
EcosimStats      { int32 struct_size; uint64 generation; int32 alive_count; int32 w; int32 h;
                   int32 species_count[ECOSIM_MAX_SPECIES]; }
EcosimSeedConfig { int32 struct_size; int32 species; uint32 density_permille; uint64 seed; }

// 函数
int32_t      ecosim_abi_version(void);
int32_t      ecosim_abi_struct_size(int32_t which);          // native 侧 sizeof，供 C# 断言布局
const char*  ecosim_last_error(void);
EcosimWorld* ecosim_world_create(const EcosimConfig* cfg);   // NULL + last_error 表示失败
void         ecosim_world_destroy(EcosimWorld*);
int32_t      ecosim_world_step(EcosimWorld*, int32_t n_steps);
int32_t      ecosim_set_topology(EcosimWorld*, int32_t topology);   // 附录 A.1：热改，状态无损
int32_t      ecosim_world_grid_view(const EcosimWorld*, EcosimGridView* out);
int32_t      ecosim_render_frame(const EcosimWorld*, uint8_t* bgra, int32_t row_pitch, const EcosimPalette* pal);
int32_t      ecosim_paint(EcosimWorld*, const EcosimPaintOp* ops, int32_t count);
int32_t      ecosim_clear(EcosimWorld*, int32_t fill_species);
int32_t      ecosim_randomize(EcosimWorld*, const EcosimSeedConfig* cfg);
int32_t      ecosim_stats(const EcosimWorld*, EcosimStats* out);
int32_t      ecosim_rle_size(const EcosimWorld*, int32_t* out_needed);   // 含结尾 \0
int32_t      ecosim_to_rle(const EcosimWorld*, char* buf, int32_t cap, int32_t* out_written);
int32_t      ecosim_from_rle(EcosimWorld*, const char* rle);
uint64_t     ecosim_hash(const EcosimWorld*);                // species 平面的 FNV-1a，测试与 CLI 用
```

`EcosimCore` C# DTO 与上表逐字段同名同序（`int`/`uint`/`ulong`/`byte[]` 固定数组用 `[MarshalAs(UnmanagedType.ByValArray, SizeConst = n)]`）。

## 文件结构

```
native/
  include/ecosim_abi.h        C ABI 唯一契约（所有 struct/enum/函数）
  src/core/grid.h|cpp         SoA 平面、cur/nxt 双缓冲、swap、resize、坐标 helper
  src/core/world.h|cpp        World 类：config、stats、paint、randomize、hash
  src/core/kernel.h|cpp       邻域计数 + B/S 演化：reference（naive）与 fast（popcount）两个入口
  src/core/parallel.h|cpp     行带切分 + std::jthread 池（threads 参数驱动）
  src/core/rle.h|cpp          RLE 解析/序列化（独立于 World，纯函数，便于单测）
  src/abi/ecosim_abi.cpp      extern "C" 薄壳：校验 struct_size、捕获异常、set last_error
  src/util/error.h|cpp        thread_local 错误串
  src/util/rng.h              xoshiro256** + splitmix64 seed（可复现）
  tools/ecosim-cli/main.cpp   headless: run / bench / hash 子命令
  tests/check.h               极简断言注册宏（ECOSIM_TEST / EXPECT_*），无外部框架
  tests/test_world.cpp        生命周期、struct_size、错误路径
  tests/test_kernel.cpp       Life 规则：still life / blinker / glider / 拓扑 / 自定义 B-S
  tests/test_parallel.cpp     reference vs fast vs 单/多线程一致性
  tests/test_rle.cpp          解析、序列化、往返、错误输入
  tests/test_render.cpp       palette → BGRA 字节级断言
  CMakeLists.txt
app/
  Ecosim.sln
  Ecosim.Core/
    Ecosim.Core.csproj
    Interop/NativeMethods.cs      全部 DllImport，仅此一处
    Interop/NativeLibraryLoader.cs DllImportResolver + ECOSIM_NATIVE_DIR
    Abi/{AbiConstants,EcosimConfigDTO,EcosimGridViewDTO,EcosimPaletteDTO,EcosimPaintOpDTO,EcosimStatsDTO,EcosimSeedConfigDTO}.cs
    NativeWorld.cs                SafeHandle：句柄 + 所有 native 调用（内部 Mutex 串行化）
    World.cs                      托管门面：Step/Paint/Clear/Randomize/RenderFrame/Stats/ToRle/FromRle
    FrameBuffer.cs                复用的 BGRA 缓冲，尺寸变化才重分配
    Viewport.cs                   纯结构：pan/zoom 与 屏幕↔网格 坐标换算（无 UI 依赖，可单测）
    SimClock.cs                   后台步进循环 + 帧发布（C# 拥有节奏）
    Rle/RleCodec.cs               托管 RLE 解析（导入路径，独立于 native，双向可测）
    Patterns/PatternLibrary.cs    内嵌 + data/patterns/*.rle 外置图案
    EcosimException.cs
  Ecosim.App/
    Ecosim.App.csproj  Program.cs  App.axaml(.cs)
    MainWindow.axaml(.cs)
    ViewModels/{ObservableObject,RelayCommand,MainViewModel,CanvasViewModel}.cs
    Rendering/CanvasRenderer.cs   WriteableBitmap + ecosim_render_frame 直写后缓冲
    ViewLocator.cs（可选）
  Ecosim.Tests/
    Ecosim.Tests.csproj
    NativeLoadTests.cs  AbiLayoutTests.cs  WorldTests.cs  ViewportTests.cs
    RleCodecTests.cs  PatternLibraryTests.cs  SimClockTests.cs  ConcurrencyTests.cs
data/patterns/{blinker,block,glider,lwss,acorn,pulsar,gosper_gun,r_pentomino}.rle
scripts/{configure.sh,build-native.sh,build-managed.sh,build.sh,test.sh,run.sh,bench.sh}
docs/{architecture.md,building.md}
.gitignore  README.md  CLAUDE.md
```

一个文件一个职责；`grid/kernel/parallel/world` 各自可独立测试；跨边界知识全部集中在 `ecosim_abi.h` ↔ `app/Ecosim.Core/Abi/`。

---

## M0 — 骨架

### Task 1: native 骨架与 C ABI 最小闭环

**Files:**
- Create: `native/include/ecosim_abi.h`, `native/src/util/error.h`, `native/src/util/error.cpp`, `native/src/core/grid.h`, `native/src/core/grid.cpp`, `native/src/core/world.h`, `native/src/core/world.cpp`, `native/src/abi/ecosim_abi.cpp`, `native/tests/check.h`, `native/tests/test_world.cpp`, `native/CMakeLists.txt`, `scripts/configure.sh`, `scripts/build-native.sh`, `.gitignore`
- Modify: 无

**Interfaces:**
- Consumes: 无（起点）
- Produces: `ecosim_abi_version()`、`ecosim_abi_struct_size(which)`、`ecosim_last_error()`、`ecosim_world_create(cfg)`、`ecosim_world_destroy(w)`、`ecosim_world_step(w,n)`、`ecosim_world_grid_view(w,&out)`；`ecosim::Grid`（`int32_t w,h; std::vector<uint8_t> species, resource; std::vector<uint16_t> energy, age;` + `Grid nxt`，方法 `size_t index(int x,int y) const`、`void allocate(int w,int h)`、`void swap_buffers()`）；`ecosim::World`（`explicit World(const EcosimConfig&)`、`int32_t step(int32_t n)`、`uint64_t generation() const`、`const EcosimConfig& config() const`）。本任务 `step` 只做 `generation += n` 与 `nxt ← cur` 的拷贝，规则留给 Task 4。

- [ ] **Step 1: 写失败测试** `native/tests/test_world.cpp`

```cpp
ECOSIM_TEST(abi_version_is_one) { EXPECT_EQ_i32(1, ecosim_abi_version()); }
ECOSIM_TEST(config_struct_size_matches_native) {
  EXPECT_EQ_i32((int32_t)sizeof(EcosimConfig), ecosim_abi_struct_size(ECOSIM_STRUCT_CONFIG));
}
ECOSIM_TEST(world_create_step_destroy) {
  EcosimConfig cfg{}; cfg.struct_size = sizeof(EcosimConfig); cfg.w = 8; cfg.h = 8;
  cfg.mode = ECOSIM_MODE_BIRSURV; cfg.topology = ECOSIM_TOPOLOGY_TORUS; cfg.threads = 1;
  EcosimWorld* w = ecosim_world_create(&cfg);
  EXPECT_TRUE(w != nullptr);
  EXPECT_EQ_i32(ECOSIM_OK, ecosim_world_step(w, 3));
  EcosimGridView v{}; v.struct_size = sizeof(v);
  EXPECT_EQ_i32(ECOSIM_OK, ecosim_world_grid_view(w, &v));
  EXPECT_EQ_i32(8, v.w); EXPECT_EQ_i32(8, v.h);
  EXPECT_EQ_i64(3, (int64_t)ecosim_stats_gen(w));   // 见 Step 3 注：本步用 generation 读取
  ecosim_world_destroy(w);
}
ECOSIM_TEST(world_create_rejects_bad_dims_and_struct_size) {
  EcosimConfig cfg{}; cfg.struct_size = sizeof(EcosimConfig); cfg.w = 0; cfg.h = 8;
  EXPECT_TRUE(ecosim_world_create(&cfg) == nullptr);
  EXPECT_TRUE(ecosim_last_error()[0] != '\0');
  cfg.w = 9000;                                    // > ECOSIM_MAX_DIM
  EXPECT_TRUE(ecosim_world_create(&cfg) == nullptr);
  cfg.w = 8; cfg.struct_size = 4;
  EXPECT_TRUE(ecosim_world_create(&cfg) == nullptr);
}
```

同时写 `native/tests/check.h`（`ECOSIM_TEST` 自注册 + `main` 跑全部、失败返回非 0、打印每条 PASS/FAIL）——测试基础设施在本任务落地，后续任务只加用例。

- [ ] **Step 2: 运行确认失败** — Run: `scripts/build-native.sh` — Expected: 编译失败（找不到 `ecosim_abi.h` / 未定义符号）。

- [ ] **Step 3: 实现 ABI 头 + Grid + World + 薄壳 + CMake**

`ecosim_abi.h` 按上文"最终 M1 ABI 面"定义本任务涉及的子集（`EcosimConfig`/`EcosimGridView`/枚举/`abi_version`/`abi_struct_size`/`last_error`/`world_*`），字段顺序与名字不得改；`#ifdef _WIN32` 下 `ECOSIM_API` 用 `__declspec(dllexport/dllimport)` + `__cdecl`。`test_world.cpp` 里的 `ecosim_stats_gen` 在本任务用一个最小导出 `int64_t ecosim_generation(const EcosimWorld*)` 代替（Task 10 引入完整 `EcosimStats` 后删除它）。`grid.cpp::allocate` 一次性把 4 个平面按 `w*h` 分配并清零（含 M2 才用的 `resource/energy/age`，避免以后重构）。

`native/CMakeLists.txt`：

```cmake
cmake_minimum_required(VERSION 3.28)
project(cell_ecosim CXX)
set(CMAKE_CXX_STANDARD 20) ; set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_library(ecosim SHARED ...)               # 见下：PREFIX "" / SUFFIX ".dll"
set_target_properties(ecosim PROPERTIES PREFIX "" OUTPUT_NAME ecosim SUFFIX ".dll"
                          CXX_VISIBILITY_PRESET default)
target_link_options(ecosim PRIVATE -static -static-libgcc -static-libstdc++)
add_executable(ecosim_tests tests/...) ; target_link_libraries(ecosim_tests PRIVATE ecosim)
enable_testing() ; add_test(NAME native_core COMMAND ecosim_tests)
```

`scripts/configure.sh` = `cmake -S native -B build/native -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release`；`scripts/build-native.sh` = configure（若无缓存）+ `cmake --build build/native -j` + `ctest --test-dir build/native --output-on-failure`。

- [ ] **Step 4: 运行确认通过** — Run: `scripts/build-native.sh` — Expected: `build/native/ecosim.dll` 存在，ctest `100% tests passed`（MSYS2 运行时依赖检查：`ldd build/native/ecosim.dll | grep -i -E "libwinpthread|libstdc++|libgcc"` 无输出）。

- [ ] **Step 5: 提交** — `git add native scripts .gitignore && git commit -m "feat(native): add C ABI skeleton, SoA grid and test harness"`

### Task 2: C# 解决方案、interop 层与布局对齐测试

**Files:**
- Create: `app/Ecosim.sln`, `app/Ecosim.Core/Ecosim.Core.csproj`, `app/Ecosim.Core/Interop/NativeMethods.cs`, `app/Ecosim.Core/Interop/NativeLibraryLoader.cs`, `app/Ecosim.Core/Abi/AbiConstants.cs`, `app/Ecosim.Core/Abi/EcosimConfigDto.cs`, `app/Ecosim.Core/Abi/EcosimGridViewDto.cs`, `app/Ecosim.Core/EcosimException.cs`, `app/Ecosim.Core/NativeWorld.cs`, `app/Ecosim.Core/World.cs`, `app/Ecosim.Tests/Ecosim.Tests.csproj`, `app/Ecosim.Tests/NativeLoadTests.cs`, `app/Ecosim.Tests/AbiLayoutTests.cs`, `app/Ecosim.Tests/WorldTests.cs`, `scripts/build-managed.sh`, `scripts/test.sh`
- Modify: `native/include/ecosim_abi.h`（无需改；若发现 pack 不一致则修 native 侧）

**Interfaces:**
- Consumes: Task 1 的全部导出。
- Produces: `Ecosim.Core.NativeWorld : SafeHandle` — `static NativeWorld Create(WorldConfig cfg)`、`void Step(int n)`、`ReadOnlySpan<byte> SpeciesPlane()`、`int Width/Height`、`long Generation`、`void Dispose()`；`Ecosim.Core.World` — `World(WorldConfig cfg)`、`Step(int)`、`Generation`、`AliveCount`（Task 10 前返回 `-1` 由 grid 统计代替）、`View()`；DTO `WorldConfig { int Width, Height; RuleMode Mode; Topology Topology; int Threads; byte[] BirthMask, SurviveMask; ulong Seed }`（`WorldConfig` 是托管 record/struct，`Abi/EcosimConfigDto` 才是固定布局）。`NativeLibraryLoader.Install()` 由 `NativeWorld` 静态构造调用。

- [ ] **Step 1: 写失败测试** `app/Ecosim.Tests/AbiLayoutTests.cs`

```csharp
[Theory]
[InlineData(ECOSIM_STRUCT_CONFIG, typeof(EcosimConfigDto))]
[InlineData(ECOSIM_STRUCT_GRID_VIEW, typeof(EcosimGridViewDto))]
public void ManagedStructSizeMatchesNative(int which, Type dto) {
    Assert.Equal(UnsafeAbi.abi_struct_size(which), Marshal.SizeOf(dto));
}
[Fact] public void ConfigFieldOffsetsMatchNative() {
    // pack=4：5×int32(0..19) | birth_mask[9]@24 | survive_mask[9]@33 | pad | uint64@44 → 52
    Assert.Equal(52, Marshal.SizeOf<EcosimConfigDto>());
    Assert.Equal(20, Marshal.OffsetOf<EcosimConfigDto>("threads").ToInt32());
    Assert.Equal(24, Marshal.OffsetOf<EcosimConfigDto>("birth_mask").ToInt32());
    Assert.Equal(33, Marshal.OffsetOf<EcosimConfigDto>("survive_mask").ToInt32());
    Assert.Equal(44, Marshal.OffsetOf<EcosimConfigDto>("rng_seed").ToInt32());
}
```

`NativeLoadTests.cs`：`NativeWorld.Create` 能拿到句柄、`Dispose` 后调用抛 `ObjectDisposedException`、`ECOSIM_NATIVE_DIR` 指向假目录时抛 `EcosimException` 且消息含"原生引擎未加载"（覆盖 Review Focus #1 的加载分支）。`WorldTests.cs`：`Step(3).Generation == 3`；`new World({Width:0})` 抛 `EcosimException`。

- [ ] **Step 2: 运行确认失败** — Run: `bash scripts/build-managed.sh` — Expected: `dotnet build` 失败（未定义 `NativeWorld`）。

- [ ] **Step 3: 实现 interop 层**

`NativeMethods` 集中 `[DllImport("ecosim", CallingConvention = CallingConvention.Cdecl)]`；`NativeLibraryLoader` 用 `NativeLibrary.SetDllImportResolver`，解析顺序：`ECOSIM_NATIVE_DIR` 环境变量 → `AppContext.BaseDirectory/ecosim.dll` → `../../build/native/ecosim.dll`（开发时从 `bin/Debug/net8.0` 回溯仓库根）。失败时抛 `EcosimException("原生引擎未加载：...")` 并附上尝试过的路径。`Ecosim.Core.csproj` 用 `<None Include="$(MSBuildThisFileDirectory)../../build/native/ecosim.dll" Pack="false" CopyToOutputDirectory="PreserveNewest" Link="ecosim.dll" />`，并定义属性 `EcosimNativeDir` 允许覆盖（`build-managed.sh` 传 `-p:EcosimNativeDir=...`）。`NativeWorld` 内部一把 `object _gate`，每个方法 `lock` 后调 native（Review Focus #5 的串行化落点）；`Step` 用 `NativeMethods` 返回码 → 非 `ECOSIM_OK` 时抛 `EcosimException($"{op} 失败({rc}): {last_error}")`。

- [ ] **Step 4: 运行确认通过** — Run: `bash scripts/test.sh` — Expected: `dotnet test` 全绿；`AbiLayoutTests` 至少 4 条 PASS。

- [ ] **Step 5: 提交** — `git add app scripts && git commit -m "feat(managed): add P/Invoke core with ABI layout and loader tests"`

### Task 3: Avalonia 应用外壳 + SimClock（M0 出口）

**Files:**
- Create: `app/Ecosim.App/Ecosim.App.csproj`, `app/Ecosim.App/Program.cs`, `app/Ecosim.App/App.axaml`, `app/Ecosim.App/App.axaml.cs`, `app/Ecosim.App/MainWindow.axaml`, `app/Ecosim.App/MainWindow.axaml.cs`, `app/Ecosim.App/ViewModels/ObservableObject.cs`, `app/Ecosim.App/ViewModels/RelayCommand.cs`, `app/Ecosim.App/ViewModels/MainViewModel.cs`, `app/Ecosim.Core/SimClock.cs`, `scripts/build.sh`, `scripts/run.sh`, `docs/building.md`, `CLAUDE.md`, `README.md`
- Modify: `app/Ecosim.sln`（加入 App 项目）

**Interfaces:**
- Consumes: `World`、`NativeWorld`。
- Produces: `Ecosim.Core.SimClock` — `SimClock(World w, int targetGenPerSec)`、`bool IsRunning`、`event EventHandler<TickEventArgs>? Tick`（`TickEventArgs { long Generation; int AliveCount; }`）、`void Start()`、`void Stop()`、`void SetSpeed(int genPerSec)`、`Task StopAsync()`；`MainViewModel` — 属性 `Generation`/`StatusText`/`Speed`/`IsRunning`，命令 `ToggleRun`、`Step`、`Clear`。`scripts/build.sh` = build-native + build-managed；`scripts/run.sh` = `dotnet run --project app/Ecosim.App`。

- [ ] **Step 1: 写失败测试** `app/Ecosim.Tests/SimClockTests.cs`

```csharp
[Fact] public async Task Clock_advances_generation_and_stops_cleanly() {
    using var w = new World(new WorldConfig { Width = 16, Height = 16 });
    using var clock = new SimClock(w, targetGenPerSec: 200);
    var tcs = new TaskCompletionSource<long>();
    clock.Tick += (_, e) => { if (e.Generation >= 20) tcs.TrySetResult(e.Generation); };
    clock.Start();
    Assert.True(await tcs.Task.WaitAsync(TimeSpan.FromSeconds(5)), "generation 未推进");
    await clock.StopAsync();
    var g = w.Generation; await Task.Delay(200);
    Assert.Equal(g, w.Generation);                 // 停住后不再推进
    Assert.False(clock.IsRunning);
}
[Fact] public void SetSpeed_zero_is_rejected() {
    using var w = new World(new WorldConfig { Width = 8, Height = 8 });
    using var clock = new SimClock(w, 10);
    Assert.Throws<ArgumentOutOfRangeException>(() => clock.SetSpeed(0));
}
```

- [ ] **Step 2: 运行确认失败** — Run: `bash scripts/test.sh` — Expected: `SimClockTests` 编译失败（无 `SimClock`）。

- [ ] **Step 3: 实现 SimClock + 手写 MVVM 基类 + 外壳**

`SimClock`：单一后台 `Task` + `PeriodicTimer`，每 tick 调 `w.Step(catchUp)`（`catchUp = max(1, 目标速率 × 经过秒数)`，允许落后追赶但不排队无限累积，上限 16/tick）；`Tick` 事件在 `SynchronizationContext.Post` 上派发（无上下文时直接回调，测试用）。`ObservableObject`/`RelayCommand` 手写：`protected bool Set<T>(ref T field, T value, [CallerMemberName] string? name = null)`。`MainWindow.axaml` 只放状态栏：播放/暂停按钮、单步、速度 `NumericUpDown`、`TextBlock` 绑定 `Generation`/`StatusText`（中文："代 0 ｜ 已暂停"）。App 启动异常包一层：`NativeLibrary` 加载失败 → `MessageBox`（`Avalonia.MainWindow` 的 `TextBlock` 错误面板即可）显示中文错误，满足 Review Focus #1。

- [ ] **Step 4: 运行确认通过 + 冒烟目视** — Run: `bash scripts/test.sh`（全绿）；`bash scripts/run.sh` — Expected: 窗口出现，点击 ▶ 后"代"数字持续增长。用 Task 14 的检查单替代自动 UI 测试。

- [ ] **Step 5: 提交** — `git add app scripts docs README.md CLAUDE.md && git commit -m "feat(app): add Avalonia shell with MVVM base and background sim clock"`

---

## M1 — Life 内核与完整可用

### Task 4: B/S 规则内核与拓扑（reference 实现）

**Files:**
- Create: `native/src/core/kernel.h`, `native/src/core/kernel.cpp`, `native/tests/test_kernel.cpp`
- Modify: `native/src/core/world.cpp`（`step` 改为调用 kernel）、`native/CMakeLists.txt`（新源文件与 `test_kernel`）

**Interfaces:**
- Consumes: `ecosim::Grid`、`EcosimConfig`（`birth_mask`/`survive_mask`/`topology`）。
- Produces: `ecosim::kernel::step_reference(Grid& cur, Grid& nxt, const RuleTable& rules, Topology topo)`、`struct RuleTable { std::array<uint8_t,9> birth, survive; static RuleTable life(); }`（`life()` = B3 / S23）、`ecosim::kernel::count_alive(const Grid&)`、`std::vector<Point> ecosim::kernel::alive_cells(const Grid&)`（测试与 paint 用）、**新增导出 `int32_t ecosim_set_topology(EcosimWorld*, int32_t topology)`**（设计附录 A.1：拓扑可热改，状态无损；同步进 `app/Ecosim.Core` 的 `World.Topology` 属性 setter）。`World::set_cells_for_test(...)` 仅供测试的直写入口（通过 `EcosimPaintOp` 走 `ecosim_paint` 的占位实现亦可，正式 paint 在 Task 10）。

- [ ] **Step 1: 写失败测试** `native/tests/test_kernel.cpp`（所有期望坐标都是手算的，不许"跑一遍再抄结果"）

```cpp
// 用 8x8/bounded 等小网格，set_cells → step(1) → alive_cells 比对
ECOSIM_TEST(block_is_still_life)                 // 2x2 方块：一步后集合不变
ECOSIM_TEST(blinker_period_two)                  // 5x5 torus (1,2)(2,2)(3,2) → 竖 blinker → 两步回到初态
ECOSIM_TEST(glider_shifts_one_diag_over_four_steps)  // 8x8 torus 标准滑翔机，4 步后 == 初始集合每个坐标 +(1,1)
ECOSIM_TEST(bounded_edge_blinker_dies_to_center_then_extinct)
    // 5x5 bounded，(0,0)(1,0)(2,0) → 一步 {(1,0)} → 两步 {}（边缘视为永久死亡）
ECOSIM_TEST(torus_wraps_vertical_blinker_across_top_edge)
    // 5x5 torus，(0,0)(1,0)(2,0) → 一步 == {(1,4),(1,0),(1,1)}  ← 跨越 y=0 的 wrap
ECOSIM_TEST(custom_birth_mask_one_seven_is_honored)
    // birth_mask[1]=birth_mask[7]=1 其余 0，survive 全 0：单格 → 一步出现 1 邻居扩张，验证 mask 真被读取
ECOSIM_TEST(alive_count_tracks_population)
ECOSIM_TEST(set_topology_takes_effect_immediately)  // 附录 A.1：同一 world 上 torus→bounded，边缘 blinker 下一步即按有界规则死亡
```

- [ ] **Step 2: 运行确认失败** — Run: `scripts/build-native.sh` — Expected: `test_kernel` 用例 FAIL（`step` 只拷贝缓冲，集合不变）。

- [ ] **Step 3: 实现 reference kernel**

naive 8 邻居循环 + `int count`，`topology == TORUS` 用 `((x+dx+w)%w)`，`BOUNDED` 越界视为死格；`birth`/`survive` 直接查 `RuleTable`。同时给 `World::step` 接上：`kernel::step_reference(cur,nxt,...)` → `swap_buffers()` → `generation += n`。不在此任务做任何性能优化。

- [ ] **Step 4: 运行确认通过** — Run: `scripts/build-native.sh` — Expected: ctest 全绿，其中 `test_kernel` 7 条用例 PASS。

- [ ] **Step 5: 提交** — `git commit -am "feat(native): implement B/S life kernel with torus and bounded topology"`

### Task 5: popcount 快速内核 + 与 reference 等价

**Files:**
- Create: `native/src/core/kernel_fast.h`, `native/src/core/kernel_fast.cpp`, `native/tests/test_fast_kernel.cpp`
- Modify: `native/src/core/world.cpp`（`threads<=1` 时走 fast 路径）、`native/src/core/parallel.h/.cpp`（本任务先建空壳 `struct BandPlan { int bands; }` 或直接把并行留到 Task 6）、`native/CMakeLists.txt`

**Interfaces:**
- Consumes: `Grid`、`RuleTable`、`Topology`。
- Produces: `ecosim::kernel::step_fast(Grid&, Grid&, const RuleTable&, Topology)`、`uint8_t ecosim::kernel::neighbor_mask(const Grid&, int x, int y, Topology)`（8-bit：bit0..bit7 对应固定顺序，文档注释写明顺序）、`int ecosim::kernel::popcount8(uint8_t)`（256 项查表）。

- [ ] **Step 1: 写失败测试** `native/tests/test_fast_kernel.cpp`

```cpp
ECOSIM_TEST(fast_matches_reference_on_random_256_seed_12345)
    // 两个 world 同 seed/config，randomize(density 300‰, seed 12345)（Task 10 前用内部 rng.h 直写 cur 平面），
    // 各跑 20 步，断言 hash 相等 → 用本任务临时导出 int32_t ecosim_test_hash(...)? 否：
    // 直接逐平面 memcmp(cur.species, other.species, w*h)
ECOSIM_TEST(fast_matches_reference_torus_and_bounded_on_blipper_64)
ECOSIM_TEST(neighbor_mask_bit_order_is_stable)   // 手摆 3x3 全活 → mask == 0xFF；只上中活 → 约定 bit
ECOSIM_TEST(popcount8_table_matches_builtin_for_all_256)
```

- [ ] **Step 2: 运行确认失败** — Run: `scripts/build-native.sh` — Expected: 新用例 FAIL（`step_fast` 未实现或返回不同结果）。

- [ ] **Step 3: 实现 fast kernel**

三行滑窗 + 每格 bitmask，查 256 项 popcount 表；torus 的 wrap 用"行首尾各复制 1 列到 halo 缓冲"避免热循环取模（halo 在 `Grid` 里预分配 `(w+2)*(3)` 的 scratch，不每步 new）。

- [ ] **Step 4: 运行确认通过** — Run: `scripts/build-native.sh` — Expected: 全绿；`test_fast_kernel` 4 条 PASS。

- [ ] **Step 5: 提交** — `git commit -am "perf(native): add bitmask popcount kernel proven equal to reference"`

### Task 6: 行带并行 + 单/多线程等价

**Files:**
- Create: `native/src/core/parallel.h`, `native/src/core/parallel.cpp`, `native/tests/test_parallel.cpp`
- Modify: `native/src/core/world.cpp`, `native/src/core/grid.h`（暴露 `row_band(int y0,int h)` 视图），`native/CMakeLists.txt`

**Interfaces:**
- Consumes: `kernel::step_fast`、`Grid`。
- Produces: `ecosim::parallel::Pool` — `explicit Pool(int n_threads)`（`n_threads<=1` 时 `run_bands` 同步执行，不起线程）、`void run_bands(int total_rows, int band_rows, const std::function<void(int y0,int y1)>& body)`、`int size() const`、`~Pool()`（jthread 自动 join）；`World::step` 走 `Pool`。

- [ ] **Step 1: 写失败测试** `native/tests/test_parallel.cpp`

```cpp
ECOSIM_TEST(parallel_equals_serial_512_seed_999_threads_1_2_4_8)
    // 同 seed 网格分别在 threads=1/2/4/8 下跑 15 步，全部与 threads=1 的 species 平面逐字节相等
ECOSIM_TEST(band_plan_covers_every_row_exactly_once)
    // run_bands 记录每行被调用的次数 → 断言每行恰好 1 次（含 halo 重叠不重复写 nxt）
ECOSIM_TEST(pool_with_one_thread_runs_inline)
    // 记录执行线程 id，断言与调用线程相同
```

- [ ] **Step 2: 运行确认失败** — Run: `scripts/build-native.sh` — Expected: FAIL（`World` 仍串行、`Pool` 不存在）。

- [ ] **Step 3: 实现 Pool 与行带**

带高取 `max(64, h / (threads*4))` 且对齐 64；每带写 `nxt` 的行区间互不重叠，读 `cur` 可重叠（只读，安全）。`Pool` 用 `std::jthread` + `std::stop_token` + 原子任务索引，池在 `world_create` 建、`world_destroy` 析构，**不在 step 内创建线程**。

- [ ] **Step 4: 运行确认通过** — Run: `scripts/build-native.sh` — Expected: 全绿，含 4 条并行用例。

- [ ] **Step 5: 提交** — `git commit -am "feat(native): add row-band thread pool with serial/parallel equivalence tests"`

### Task 7: headless `ecosim-cli`（run / bench / hash）

**Files:**
- Create: `native/tools/ecosim-cli/main.cpp`, `native/tests/test_cli.bats.sh`（普通 shell 脚本即可，不引 bats；命名 `native/tests/cli_smoke.sh`）
- Modify: `native/CMakeLists.txt`（`add_executable(ecosim_cli ...)`，输出 `ecosim-cli.exe`）、`scripts/bench.sh`

**Interfaces:**
- Consumes: 全部已实现的导出（`world_create/step/grid_view/hash`；`to_rle/from_rle` 在 Task 8 后接入 `--rle`）。
- Produces: CLI 契约（UI 与文档都依赖这个拼写）：

```
ecosim-cli run   --w N --h N [--rule life|custom --birth 3 --survive 23] [--topology torus|bounded]
                 [--seed N] [--density PERMILLE] [--rle FILE] [--out FILE] --gens N [--threads N]
ecosim-cli bench --w N --h N --gens N [--threads N] [--seed N] [--rle FILE]
                 → 打印 "gens=<N> seconds=<f> gens_per_sec=<f> ms_per_gen=<f> hash=<hex>"
ecosim-cli hash  --rle FILE [--w N --h N] --gens N → 仅打印 hash
```

退出码：0 成功，2 参数错误，3 native 失败（打印 `ecosim_last_error()`）。

- [ ] **Step 1: 写失败测试** `native/tests/cli_smoke.sh`

```bash
BIN="${1:-build/native/ecosim-cli.exe}"
"$BIN" bench --w 128 --h 128 --gens 50 --seed 7 --threads 4 | grep -q 'gens_per_sec=' || fail
"$BIN" run --w 64 --h 64 --gens 3 --seed 7 --density 250 --out build/tmp.rle && [ -s build/tmp.rle ] || fail
"$BIN" bench --w 0 --h 8 --gens 1 >/dev/null 2>&1; [ $? -eq 2 ] || fail   # 参数错误
"$BIN" hash --rle nope.rle >/dev/null 2>&1; [ $? -eq 3 ] || fail
```

在 CMake 里 `add_test(NAME cli_smoke COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_smoke.sh $<TARGET_FILE:ecosim_cli>)`。

- [ ] **Step 2: 运行确认失败** — Run: `scripts/build-native.sh` — Expected: `cli_smoke` FAIL（可执行文件不存在）。

- [ ] **Step 3: 实现 main.cpp**

手写参数解析（不用第三方库）；`bench` 用 `std::chrono::steady_clock` 包住 `step(gens)` 总时长并报告四项。`scripts/bench.sh` 提供 M1 出口用的预设：`bench --w 1024 --h 1024 --gens 200 --density 300 --threads 0` 与 `--w 2048 --h 2048 --gens 50`。

- [ ] **Step 4: 运行确认通过** — Run: `scripts/build-native.sh && bash scripts/bench.sh` — Expected: 4 项指标打印，`2048²` 下 `ms_per_gen` < 100（观测记录，不作硬断言；写入 `docs/building.md` 作为基线表）。

- [ ] **Step 5: 提交** — `git commit -am "feat(tools): add headless ecosim-cli with run, bench and hash"`

### Task 8: RLE 序列化/解析 + 图案库

**Files:**
- Create: `native/src/core/rle.h`, `native/src/core/rle.cpp`, `native/tests/test_rle.cpp`, `data/patterns/{block,blinker,glider,lwss,acorn,pulsar,r_pentomino,gosper_gun}.rle`, `app/Ecosim.Core/Rle/RleCodec.cs`, `app/Ecosim.Core/Patterns/PatternLibrary.cs`, `app/Ecosim.Core/Patterns/Pattern.cs`, `app/Ecosim.Tests/RleCodecTests.cs`, `app/Ecosim.Tests/PatternLibraryTests.cs`
- Modify: `native/src/abi/ecosim_abi.cpp`（`ecosim_rle_size`/`to_rle`/`from_rle` 从 `-ENOSYS` 占位改为实现）、`native/CMakeLists.txt`、`app/Ecosim.Core/World.cs`

**Interfaces:**
- Consumes: `Grid`、`EcosimWorld`。
- Produces: `ecosim::rle::encode(const Grid&, std::string& out)` / `decode(const std::string&, Grid&, int& w, int& h)`（返回负错误码 + `last_error` 文本）；C# `RleCodec.Decode(string rle) -> RleDocument { int Width, Height, RuleSpec?; IReadOnlyList<Point> Cells }`、`RleCodec.Encode(int w,int h,IEnumerable<Point>) -> string`；`PatternLibrary.All -> IReadOnlyList<Pattern>`、`Pattern { string Id, string Name, string Rle }`（内嵌 `block/blinker/glider/lwss/acorn/pulsar/r_pentomino/gosper_gun`，外加从 `data/patterns/` 读取同名外置文件覆盖）。

- [ ] **Step 1: 写失败测试**

`native/tests/test_rle.cpp`：

```cpp
ECOSIM_TEST(rle_roundtrip_512_random_seed_42)      // encode→decode→hash 相同
ECOSIM_TEST(rle_gosper_header_and_dimensions)      // "x = 36, y = 9, rule = B3/S23" 且 decode 出 36×9
ECOSIM_TEST(rle_rejects_garbage_and_overlong_run)  // "1000000000a" / "!!!" → 负码 + 非空 last_error
ECOSIM_TEST(rle_trailing_bang_and_newlines_ok)
```

`app/Ecosim.Tests/RleCodecTests.cs`：托管解析与 native 解析对同一串产出**相同坐标集合**（`Decode` 结果 → `Paint` → `hash` 与 `from_rle` 后 `hash` 相等）；`glider.rle` 逐坐标断言（覆盖 Review Focus #4）：

```csharp
[Fact] public void Glider_rle_decodes_to_expected_cells() {
    var doc = RleCodec.Decode("x = 3, y = 3, rule = B3/S23\nbob$2bo$3o!\n");
    Assert.Equal(3, doc.Width); Assert.Equal(3, doc.Height);
    Assert.Equal(new[]{ (1,0),(2,1),(0,2),(1,2),(2,2) }, doc.Cells.OrderBy(p=>p).ToArray());
}
```

`PatternLibraryTests.cs`：`All` 含 8 个内嵌图案 id，每个 `RleCodec.Decode` 无异常且 `Cells.Count > 0`。

- [ ] **Step 2: 运行确认失败** — Run: `scripts/build-native.sh; bash scripts/test.sh` — Expected: native RLE 用例 FAIL（占位返回 `-3`），托管 `RleCodecTests` 编译失败。

- [ ] **Step 3: 实现**

native 编码器：行内连续 run 用 `<count><b|o>`，空行用 `<count>b` + `$`，结尾 `!`；解码器状态机支持 `数字/字母/$/!/#C 注释/空格与换行忽略`、`x = , y = , rule = ` 头部（`rule` 解析出来但 M1 只在 `EcosimConfig` 的 mask 里生效）。

`ecosim_from_rle` 的语义按附录 A.4.3 钉死：**在当前 world 的 (0,0) 放置图案，不改变网格尺寸**；超出网格的部分裁剪并通过 `ecosim_last_error()` 报"图案被裁剪 x 列 y 行"（返回 `ECOSIM_OK`，警告不是错误）。读档式"按存档尺寸重建"是托管 `World` 的职责（Task 13 的 Open RLE 路径），native 不管。另：RLE 严格单状态（附录 A.6），`species > 1` 的图案不写 `.rle`，属 M2 的 `.cew` 家族。

`PatternLibrary` 从 `AppContext.BaseDirectory` 向上找 `data/patterns/`（找不到就只用内嵌），用 `Assembly.GetManifestResourceStream` 之外的方案：**图案以字符串常量内嵌 + 外置文件覆盖**，保持零额外资源负担。

- [ ] **Step 4: 运行确认通过** — Run: `scripts/build-native.sh && bash scripts/test.sh` — Expected: 全绿。

- [ ] **Step 5: 提交** — `git commit -am "feat: add RLE encode/decode on both sides with a pattern library"`

### Task 9: `ecosim_render_frame`（palette → BGRA）

**Files:**
- Create: `native/tests/test_render.cpp`
- Modify: `native/src/core/world.h/.cpp`（新增 `int32_t render_frame(uint8_t* bgra, int32_t row_pitch, const EcosimPalette&) const`）、`native/src/abi/ecosim_abi.cpp`、`native/CMakeLists.txt`、`app/Ecosim.Core/Abi/EcosimPaletteDto.cs`、`app/Ecosim.Core/FrameBuffer.cs`、`app/Ecosim.Core/World.cs`

**Interfaces:**
- Consumes: `Grid.species`、`EcosimPalette.cell_bgra[16]`。
- Produces: `int32_t ecosim::World::render_frame(uint8_t* bgra, int32_t row_pitch, const EcosimPalette& pal) const`；C# `World.RenderFrame(Span<byte> dest, int rowPitch, Palette pal)`、`Palette.Default`（`species 0` = 深底 `0xFF14181D`（BGRA），`species 1` = Life 活格 / 藻 共用的 `0xFF6FD36F`，`species 2` = 虫黄 `0xFF40C8E8`，`species 3` = 兽红 `0xFF5860F0`；附录 A.7：Life 模式下活格一律 `species = 1`，不存在物种 id 4）、`FrameBuffer` — `int Width/Height`、`Span<byte> Bytes`、`int RowPitch`、`void Ensure(int w,int h)`。

- [ ] **Step 1: 写失败测试** `native/tests/test_render.cpp`

```cpp
ECOSIM_TEST(render_writes_bgra_per_species_in_row_major_order)
    // 2x2，pal: [0]=0xFF000000 [1]=0xFF112233 [2]=0xFF445566；species {0,1,2,0}
    // → 断言 4 个 4 字节槽逐字节相等，row_pitch=16 与 row_pitch=24（带尾部填充）两种都测
ECOSIM_TEST(render_respects_unknown_species_slot)  // species=7 用 pal[7]，未设置槽为 0
ECOSIM_TEST(render_rejects_undersized_pitch_and_null)  // 负 row_pitch / NULL → ECOSIM_ERR_INVALID
```

- [ ] **Step 2: 运行确认失败** — Run: `scripts/build-native.sh` — Expected: FAIL（导出返回 `ECOSIM_ERR_STATE` 或未实现）。

- [ ] **Step 3: 实现** 每格 `memcpy`/`uint32` 写入 + `row_pitch` 步进；`species > ECOSIM_MAX_SPECIES-1` 视为 0。C# 侧 `World.RenderFrame` 传 `MemoryMarshal.GetReference(dest)`。

- [ ] **Step 4: 运行确认通过** — Run: `scripts/build-native.sh && bash scripts/test.sh` — Expected: 全绿。

- [ ] **Step 5: 提交** — `git commit -am "feat: render grid to BGRA in native with a palette"`

### Task 10: `paint` / `clear` / `randomize` / `stats` + 一致性钉桩

**Files:**
- Create: `native/tests/test_paint.cpp`, `app/Ecosim.Core/Abi/EcosimPaintOpDto.cs`, `app/Ecosim.Core/Abi/EcosimStatsDto.cs`, `app/Ecosim.Core/Abi/EcosimSeedConfigDto.cs`, `app/Ecosim.Core/PaintOps.cs`, `app/Ecosim.Tests/WorldTests.cs`（追加）
- Modify: `native/src/core/world.h/.cpp`, `native/src/abi/ecosim_abi.cpp`（并删除临时 `ecosim_generation` 导出，改用 `EcosimStats`）、`native/CMakeLists.txt`、`app/Ecosim.Core/NativeWorld.cs`, `app/Ecosim.Core/World.cs`, `native/tests/test_world.cpp`（改用 stats 读 generation）

**Interfaces:**
- Consumes: `Grid`、`rng::Xoshiro`、`RuleTable`。
- Produces: native `ecosim_paint/world_clear/world_randomize/world_stats`；C# `World.Paint(IReadOnlyList<PaintOp> ops)`、`World.FillRect(int x0,int y0,int x1,int y1,int species)`、`World.Clear(int species = 0)`、`World.Randomize(int species, int densityPermille, ulong seed)`、`World.Stats -> SimStats { long Generation, int Alive, int[] SpeciesCounts }`、`PaintOp(int X0,int Y0,int X1,int Y1,int Species)`。

- [ ] **Step 1: 写失败测试**

`native/tests/test_paint.cpp`：

```cpp
ECOSIM_TEST(paint_rect_is_half_open_and_clamped)      // [x0,x1) 语义；越界返回 ECOSIM_ERR_RANGE
ECOSIM_TEST(paint_then_step_uses_painted_state)        // Review Focus #2：暂停→paint→step(1) 按规则演化
ECOSIM_TEST(randomize_is_reproducible_per_seed)        // 同 seed 两次 → hash 相同；不同 seed → 不同
ECOSIM_TEST(randomize_and_step_are_thread_count_independent) // 附录 A.5.2：threads=1 vs 4，同 seed 跑 10 代 → hash 相等
ECOSIM_TEST(randomize_density_within_tolerance)        // density 250‰ → alive/总数 ∈ [0.22,0.28]
ECOSIM_TEST(stats_reports_generation_alive_and_counts)
ECOSIM_TEST(clear_resets_generation_optional_keeps_it) // clear(0) 后 alive==0，generation 不变
```

`app/Ecosim.Tests/WorldTests.cs` 追加：

```csharp
[Fact] public void Paint_fill_rect_then_step_matches_hash_of_native_equivalent()
[Fact] public void Stats_alive_count_tracks_paint_and_clear()
[Fact] public void Out_of_range_paint_throws_ecosim_exception_with_message()   // 中文/英文消息都来自 last_error
```

- [ ] **Step 2: 运行确认失败** — Run: `scripts/build-native.sh; bash scripts/test.sh` — Expected: FAIL（占位实现/未导出）。

- [ ] **Step 3: 实现**：`paint` 在 `cur` 平面写（同时同步 `nxt ← cur`，保证暂停期绘制不会在下一步被旧 `nxt` 覆盖 → 这是 Review Focus #2 的实质修法）；`randomize` 用 `native/src/util/rng.h` 的 **counter-based 无状态随机**（附录 A.5.2）：`uint64_t value = splitmix64(seed ^ (0x9E3779B97F4A7C15ull * (generation + 1)) ^ (0xC2B2AE3D27D4EB4Full * (cell_index + 1)))`，`seed` 取 `EcosimSeedConfig.seed`（`0` 表示沿用 `EcosimConfig.rng_seed`）。**不持有序列化状态**，所以"同一格子在同一代取到的随机值与线程数/带序无关"必须成为测试：`randomize_and_step_are_thread_count_independent`（threads=1 vs 4，同 seed 同代数 → `hash` 相等）与 `randomize_is_reproducible_per_seed` 一起钉住 §14 风险表那条；`stats` 每次 O(n) 现算（M2 改为步进期增量统计）。

- [ ] **Step 4: 运行确认通过** — Run: `scripts/build-native.sh && bash scripts/test.sh` — Expected: 全绿。

- [ ] **Step 5: 提交** — `git commit -am "feat: add paint, clear, randomize and stats across the ABI"`

### Task 11: 画布渲染与视口（pan / zoom）

**Files:**
- Create: `app/Ecosim.Core/Viewport.cs`, `app/Ecosim.App/Rendering/CanvasRenderer.cs`, `app/Ecosim.App/ViewModels/CanvasViewModel.cs`, `app/Ecosim.Tests/ViewportTests.cs`
- Modify: `app/Ecosim.App/MainWindow.axaml(.cs)`（放 `Image` + `ScrollViewer` 替换为自绘视口）、`app/Ecosim.App/ViewModels/MainViewModel.cs`（组合 `CanvasViewModel`）、`app/Ecosim.App/Ecosim.App.csproj`

**Interfaces:**
- Consumes: `World.RenderFrame`、`FrameBuffer`、`SimClock.Tick`、`PatternLibrary`。
- Produces: `readonly struct Viewport { double CellSize; int OffsetX, OffsetY; Point ScreenToCell(Point p); Rect VisibleCells(int gridW,int gridH, double canvasW, double canvasH); Viewport ZoomAt(Point screen, double factor, double minCell, double maxCell); }`（`CellSize` 钳制在 `[1,64]`）；`CanvasRenderer` — `void Draw(World w, FrameBuffer fb, Viewport vp, WriteableBitmap target)`（`target.Lock()` → `ecosim_render_frame` 直写后缓冲，失败退化为 `fb` + `CopyTo`）；`CanvasViewModel` — `Viewport`、`ZoomIn/ZoomOut/FitToView`、`BeginPan/pan/EndPan`。

- [ ] **Step 1: 写失败测试** `app/Ecosim.Tests/ViewportTests.cs`

```csharp
[Fact] public void Screen_to_cell_is_floor_division_with_offset();
[Fact] public void Zoom_at_cursor_keeps_the_cell_under_the_cursor_still_under_it()
    // 断言 ZoomAt 前后 ScreenToCell(鼠标点) 落在同一格（±1e-9）
[Fact] public void Cell_size_clamps_to_1_and_64();
[Fact] public void Visible_cells_is_clipped_to_grid_bounds_at_each_corner();  // 含 offset 负值
[Fact] public void Fit_to_view_largest_cell_size_that_fits();
```

- [ ] **Step 2: 运行确认失败** — Run: `bash scripts/test.sh` — Expected: 编译失败（`Viewport` 未定义）。

- [ ] **Step 3: 实现 Viewport（纯数学，无 Avalonia 依赖，所以能在普通 xUnit 里测）+ CanvasRenderer + 视口手势**

按附录 A.2：`CanvasRenderer.Draw` 必须在 `NativeWorld._gate` 持有期间把 native 直接写进 `BitmapLock` 的后缓冲（这就是零拷贝正解，不是退化路径；借用指针与缓冲交换不会竞态，因为 step 也要同一把锁）；只有拿不到可写指针时才退回 `FrameBuffer` + `CopyTo`，并把退化路径写进 `docs/architecture.md`。`CanvasRenderer` 里 `BitmapLock` 写入前先 `fb.Ensure(vp 可见格数)`；网格线在 `CellSize >= 8` 时由 native 之外的 `DrawingContext`? — 不引入第二套绘制：网格线用 Avalonia `Rectangle` + `OpacityMask`? 决定：**网格线在 `CellSize >= 6` 时用 `WriteableBitmap` 内叠加 1px 灰线（native 不画，C# 在帧上叠）**,关掉开关 `G`。（我的决定，写进 docs/architecture.md）

- [ ] **Step 4: 运行确认通过 + 目视** — Run: `bash scripts/test.sh && bash scripts/run.sh` — Expected: 测试全绿；窗口能看到 1024×1024 网格在跑，滚轮缩放、拖拽平移跟手。

- [ ] **Step 5: 提交** — `git commit -am "feat(app): add canvas rendering with pan, zoom and viewport math"`

### Task 12: 绘制输入与并发安全

**Files:**
- Create: `app/Ecosim.Core/BrushStroke.cs`, `app/Ecosim.Tests/ConcurrencyTests.cs`
- Modify: `app/Ecosim.App/ViewModels/CanvasViewModel.cs`（`PointerPressed/Moved/Released` → `PaintOp` 批量）、`app/Ecosim.App/MainWindow.axaml.cs`、`app/Ecosim.Core/NativeWorld.cs`（确认 `_gate` 覆盖 paint/render/stats 全部路径）

**Interfaces:**
- Consumes: `World.Paint`、`Viewport`、`SimClock`。
- Produces: `static class BrushStroke { static IEnumerable<PaintOp> Line(Viewport vp, Point from, Point to, int species, int brushRadius); }`（Bresenham，去重同一格）、`CanvasViewModel.Tool`（`Draw|Erase|Pan`）、`BrushRadius`（1..32）、`HoveredCell`（`Point?`，悬停显示物种/能量/年龄）。

- [ ] **Step 1: 写失败测试**

`app/Ecosim.Tests/ConcurrencyTests.cs`（Review Focus #5）：

```csharp
[Fact] public void Concurrent_paint_and_step_and_render_never_throws_and_hash_is_deterministic() {
    using var w = new World(new WorldConfig { Width = 128, Height = 128 });
    var buf = new FrameBuffer(); var pal = Palette.Default;
    var tasks = new[] {
        Task.Run(() => { for (int i = 0; i < 400; i++) w.Step(1); }),
        Task.Run(() => { for (int i = 0; i < 400; i++) w.FillRect(i % 100, i % 100, i % 100 + 3, i % 100 + 3, 1); }),
        Task.Run(() => { for (int i = 0; i < 400; i++) { w.RenderFrame(buf.Bytes, buf.RowPitch, pal); } })
    };
    Assert.DoesNotContain(tasks.Select(t => t.Exception), x => x is not null);
}
[Fact] public void Brush_line_covers_each_cell_once_and_is_symmetric()   // (0,0)->(10,0) 11 格；反向同集合
[Fact] public void Brush_radius_3_paints_disk_of_expected_extent()
```

- [ ] **Step 2: 运行确认失败** — Run: `bash scripts/test.sh` — Expected: FAIL（`BrushStroke` 未定义；并发用例在 paint 未加锁路径上抛或结果不确定）。

- [ ] **Step 3: 实现**：输入事件里把移动轨迹攒成 `List<PaintOp>` 后**一次** `World.Paint`（一次 native 往返，避免每像素一次调用），只在 `IsPointerPressed` 且 `Tool != Pan` 时绘制；`HoveredCell` 从 `Viewport.ScreenToCell` 得到，读 `grid_view` 借用指针时同样在 `_gate` 内。

- [ ] **Step 4: 运行确认通过 + 目视** — Run: `bash scripts/test.sh && bash scripts/run.sh` — Expected: 全绿；播放中能画出稳定图案，暂停中画的格子在单步后按规则演化。

- [ ] **Step 5: 提交** — `git commit -am "feat(app): add brush painting input with lock-guarded ABI access"`

### Task 13: 图案/规则面板、状态栏与 RLE 存读

**Files:**
- Create: `app/Ecosim.App/ViewModels/InspectorViewModel.cs`, `app/Ecosim.App/Views/{ToolBar.axaml,SidePanel.axaml}.cs/.axaml`(或并入 MainWindow), `app/Ecosim.Tests/LargeGridTests.cs`
- Modify: `app/Ecosim.App/MainWindow.axaml(.cs)`（左面板：工具/笔刷/物种/图案/规则；底栏：统计与耗时）、`MainViewModel.cs`、`app/Ecosim.App/ViewModels/RelayCommand.cs`（如需异步命令）

**Interfaces:**
- Consumes: `PatternLibrary`、`RleCodec`、`World`、`SimClock`、`CanvasViewModel`。
- Produces: `MainViewModel` 增加 `IReadOnlyList<Pattern> Patterns`、`SelectedPattern`（应用图案到当前视口中心）、`Topology`、`BirthMask/SurviveMask`（文本框 "3"/"23" 解析，非法输入红框提示）、`Speed`、`GenPerSec`、`StepMs`、`AliveCount`、`SaveRleAsync()`/`OpenRleAsync()`（Avalonia `StorageProvider`，文件对话框默认名 `ecosim.rle`）。
另按附录 A.4 落地"新建世界"：`NewWorldCommand` + 侧栏宽/高输入框与 512/1024/2048/4096 预设按钮，默认 `1024×1024`，超过 `ECOSIM_MAX_DIM=8192` 时输入框红框 + 中文提示"网格尺寸超出上限 8192"；`OpenRleAsync` 走 `max(存档尺寸, 内容包围盒)` 重建 world 并在状态栏提示"已按存档尺寸重建 {W}×{H}"（与"在 (0,0) 放置图案"的 `ApplyPattern` 分列两个命令）。`Topology` 用 Task 4 的 `ecosim_set_topology` 热改，不重建 world。

- [ ] **Step 1: 写失败测试** `app/Ecosim.Tests/LargeGridTests.cs`（Review Focus #3，纯 Core 层，不起 UI）

```csharp
[Fact] public void Render_and_step_2048x2048_stay_within_budget_and_memory() {
    using var w = new World(new WorldConfig { Width = 2048, Height = 2048, Threads = 0 });
    w.Randomize(species: 1, densityPermille: 200, seed: 1234);
    var sw = Stopwatch.StartNew(); w.Step(5); var stepMs = sw.Elapsed.TotalMilliseconds;
    var fb = new FrameBuffer(); fb.Ensure(2048, 2048);
    sw.Restart(); w.RenderFrame(fb.Bytes, fb.RowPitch, Palette.Default); var renderMs = sw.Elapsed.TotalMilliseconds;
    Assert.True(stepMs / 5 < 100, $"ms_per_gen {stepMs/5:F1} 超过 100ms");
    Assert.True(renderMs < 120, $"render {renderMs:F1}ms");
    Assert.Equal(2048*2048*4, fb.Bytes.Length);
}
[Fact] public void Parse_rule_mask_text_accepts_and_rejects()   // "23"/"3" ok；"x"/"12"（>9 位）→ FormatException
```

- [ ] **Step 2: 运行确认失败** — Run: `bash scripts/test.sh` — Expected: FAIL（`Randomize` 签名不符/掩码解析未实现）。

- [ ] **Step 3: 实现面板与绑定**：状态栏文案格式钉死为 `代 {Generation:N0} ｜ 存活 {Alive:N0} ｜ {GenPerSec:N0} 代/秒 ｜ 步进 {StepMs:F1} ms ｜ {Topology}`；中文标签（"播放/暂停""单步""速度(代/秒)""拓扑：环形/有界""出生掩码 B""存活掩码 S""图案""导入 RLE""导出 RLE"）。

- [ ] **Step 4: 运行确认通过 + 目视** — Run: `bash scripts/test.sh && bash scripts/run.sh` — Expected: 全绿；选 Gosper 枪 → 播放 → 30 代后能看到滑翔机射出；2048² 播放时窗口不冻结。

- [ ] **Step 5: 提交** — `git commit -am "feat(app): add pattern, rule and status panels with RLE import/export"`

### Task 14: M1 出口验证与文档

**Files:**
- Create: `docs/architecture.md`, `docs/m1-checklist.md`
- Modify: `README.md`（截图位/构建步骤/已知限制）、`CLAUDE.md`

- [ ] **Step 1: 跑全套** — Run: `bash scripts/build.sh && bash scripts/test.sh && bash scripts/bench.sh` — Expected: native + managed 全绿；`bench` 打印 1024²/2048² 基线表。

- [ ] **Step 2: 性能门槛核对**（设计 §2 的 M1 出口）：`ecosim-cli bench --w 1024 --h 1024 --rle data/patterns/gosper_gun.rle --gens 5000 --threads 0` → `ms_per_gen < 16.7`（≥60 FPS）。不达标则回到 Task 5/6 调 halo 与带高，不改架构。把实测数字写进 `docs/m1-checklist.md` 的表格里。

- [ ] **Step 3: 目视清单核对** `docs/m1-checklist.md`：▶/⏸/单步、速度、2048² 不冻结、缩放 1x–64x、平移、画/擦/半径、图案全部 8 个可用、RLE 导入导出往返 `hash` 不变、拓扑切换、自定义 B/S 生效（HighLife `B36/S23` 能看到复制者）、错误文案（临时改名 `ecosim.dll` 后启动）。

- [ ] **Step 4: 写文档** `docs/architecture.md`：边界纪律（struct_size、pack4、借用指针有效期、单写者 `_gate`）、目录职责、如何加一个物种（M2 预告）、如何加一个参数。`README.md`：中文简介 + 构建/运行/测试 + 截图占位。`CLAUDE.md`：常用命令 + 边界规则 + "native 是唯一计算层，UI 无模拟逻辑"。

- [ ] **Step 5: 提交** — `git commit -am "docs: record M1 performance baseline, checklist and architecture"`

---

## M2 / M3 / M4

不在本计划内。M1 交付后按同一流程另写计划：

- **M2 多物种生态**：`EcosimParams` + `EcosimParamDesc`（参数描述表 → UI 自动生成滑杆）、六阶段管线（sense/feed/metabolise/reproduce/disperse/regrow）、物种表、`species_count` 统计、种群折线。出口：固定种子下断言 ≥2 次捕食-猎物振荡。
- **M3 可视化与分析**：叠加层（能量/resource/物种）、直方图、native 事件日志（灭绝/激增）、快照环形缓冲与时间轴 scrubber/回放。
- **M4 打磨**：`cew` 存档、矢量降级缩放、快捷键完备、错误与日志体验、并行度自动选择、性能预算 8 ms。
