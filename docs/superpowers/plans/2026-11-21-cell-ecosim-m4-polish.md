# Cell Ecosim — M4 存档、增量统计与打磨 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把生态状态存成可回读的二进制存档（`cew`），把面板的每 250 ms 全平面拷贝换成引擎侧增量统计，修掉 M3 留下的三处已知问题（回放滑块停靠、`Watermark` 废弃警告、快照钉住无快捷键），并把性能余量重新量一遍。

**Architecture:** `cew` 是版本化容器：magic + version + `EcosimConfig` + `EcosimParams` + generation + 四个平面的 RLE（species/energy/resource 存，age 不存，与快照环同一取舍）。引擎顺手算能量直方图（普查那一遍已经在遍历细胞），面板只读 12 个整数。UI 侧修绑定与快捷键，不动架构。

**Tech Stack:** 同 M1–M3：C++20 / MinGW / CMake / Avalonia 12.1.6 / net8.0 / xUnit / CTest。

**Spec:** `docs/superpowers/specs/2026-11-21-cell-ecosim-design.md` §12 存档格式、附录 A.4/A.5/A.6；前置：`...-m0-m1.md`、`...-m2-ecosystem.md`、`...-m3-visualization.md`（均已交付，见 `docs/m1-checklist.md`、`docs/m2-checklist.md`、`docs/m3-checklist.md`）。

## Global Constraints

- 继承 M1 计划的全部 Global Constraints。
- `cew` 容器必须**自描述且版本化**：magic `CEWS`、`uint16 version`、`uint16 header_size`；读旧版本时靠 `struct_size` 前缀容忍，不靠猜。
- 引擎侧新增的每代开销必须重新过一遍性能门槛（生态 2048² 余量只剩 1.1×）：任何"每代多做一遍"的改动都要带跑分对比。
- 直方图必须由已有的普查遍历顺手产出，**不允许**为画图再全表扫一遍。
- 存档 I/O 归调用方（托管侧读写文件），native 只吃/吐字节缓冲（设计 §7）。

## Review Focus

- **尺寸不匹配的存档**：打开一个 1024² 的存档到 512² 世界，必须报出"需要 1048576 格，当前 262144"，而不是截断或越界写。
- **版本与魔数**：截断文件、错 magic、未来版本号都要给出可区分的中文错误，不能把损坏当空档。
- **参数一起回来**：`cew` 必须恢复 `EcosimParams`，否则"读档后行为不同"是最难查的 bug。
- **直方图与快照一致性**：同一代问两次必须逐桶相等（增量统计最容易在 swap 时漏更新）。
- **回放滑块**：`Maximum` 变化不得把 Value 顶到最大（M3 已知问题的根因假设）。

---

### Task 1: `cew` 容器（native 编解码）

**Files:**
- Create: `native/src/core/cew.h/.cpp`, `native/tests/test_cew.cpp`
- Modify: `native/include/ecosim_abi.h`（`ecosim_cew_size` / `ecosim_save_cew` / `ecosim_load_cew`）、`native/src/abi/ecosim_abi.cpp`、`native/src/core/world.h/.cpp`、`native/CMakeLists.txt`

**Interfaces:**
- Produces:
  `struct CewHeader { char magic[4]; uint16_t version; uint16_t header_size; uint32_t w; uint32_t h; uint64_t generation; uint32_t body_size; uint8_t plane_modes[3]; uint8_t reserved[9]; }`（40 字节）
  `int32_t cew::encode(const World&, std::vector<uint8_t>& out)`
  `int32_t cew::decode(const uint8_t* data, size_t size, World&, std::string& why)`
  ABI：`ecosim_cew_size(world, int32_t* out)`、`ecosim_save_cew(world, uint8_t* buf, int32_t cap, int32_t* written)`、`ecosim_load_cew(world, const uint8_t* buf, int32_t len)`。
  错误码：坏 magic / 版本 / 截断 → `ECOSIM_ERR_INVALID`；尺寸不符 → `ECOSIM_ERR_RANGE`；缓冲不足 → `ECOSIM_ERR_SIZE`。

- [ ] **Step 1: 写失败测试**（`test_cew.cpp`，全部手构造）
  - `roundtrip_restores_cells_energy_resource_generation_and_params`
  - `roundtrip_is_byte_stable`（同一状态编两次得到同一串）
  - `truncated_buffer_is_reported_as_corrupt_not_empty`
  - `wrong_magic_and_future_version_say_different_things`
  - `size_mismatch_names_both_numbers`
  - `params_survive_the_roundtrip`（把 light 改成 0 存盘、重置世界、读回，仍是 0）
- [ ] **Step 2: 运行确认失败**
- [ ] **Step 3: 实现**（复用快照环那套 plane 编码；把 `encode_plane/decode_plane` 提到 `core/plane_codec.h` 供两处共用）
- [ ] **Step 4: 运行确认通过**
- [ ] **Step 5: 提交** — `feat(native): add the versioned cew container`

### Task 2: 托管侧 `cew` 存读 + UI

**Files:**
- Create: `app/Ecosim.Core/Cew.cs`, `app/Ecosim.Tests/CewTests.cs`
- Modify: `app/Ecosim.Core/Interop/NativeMethods.cs`, `NativeWorld.cs`, `World.cs`, `MainViewModel.cs`（导入/导出命令）、`MainWindow.axaml`

- [ ] **Step 1: 写失败测试**：`CewTests` — 存→读→哈希一致、代数一致、参数一致；截断字节流抛 `EcosimException` 且消息含"损坏"；尺寸不符的消息含两个数字；`.rle` 与 `.cew` 走不同解码路径（RLE 读不到 cew，反之亦然）。
- [ ] **Step 2: 运行确认失败**
- [ ] **Step 3: 实现**（`World.SaveCew()` / `World.LoadCew(byte[])`；UI 加"存档 / 读档"两个按钮，默认名 `ecosim.cew`；生态模式下导出 RLE 要有损提示）
- [ ] **Step 4: 运行确认通过 + 目视**
- [ ] **Step 5: 提交** — `feat(app): save and load .cew worlds`

### Task 3: 引擎侧能量直方图（去掉面板的全平面拷贝）

**Files:**
- Create: `native/tests/test_histogram.cpp`
- Modify: `native/src/core/history.h/.cpp`（`SpeciesCounts` 旁边加 `EnergyCounts`）、`native/src/core/kernel_fast.*`、`native/src/rules/ecosystem.cpp`、`native/src/core/world.h/.cpp`、`native/include/ecosim_abi.h`（`ecosim_energy_histogram`）、`app/Ecosim.Core/World.cs`、`MainViewModel.cs`

**Interfaces:**
- Produces: `void World::energy_histogram(int32_t buckets, int32_t* out) const`（读引擎缓存的最后一代直方图）；
  直方图在普查那一遍顺手算出（每带局部数组，结束后合并，避免 M3 学到的 false sharing）。

- [ ] **Step 1: 写失败测试**：`histogram_matches_the_managed_recount`（同一代，native 直方图与托管从快照算出的逐桶相等）；`histogram_survives_a_swap`（连续 20 代每代查询都自洽）；`bucket_count_clamps`。
- [ ] **Step 2: 运行确认失败**
- [ ] **Step 3: 实现**
- [ ] **Step 4: 运行确认通过**，并重跑 `scripts/bench.sh` + 混合网探针，把前后数字写进 `docs/m4-checklist.md`（门槛不许退）
- [ ] **Step 5: 提交** — `perf(native): compute the energy histogram during the census pass`

### Task 4: M3 遗留三处打磨

**Files:**
- Modify: `app/Ecosim.App/MainWindow.axaml`（滑块停靠、`PlaceholderText`、钉住快照快捷键）、`MainViewModel.cs`（`ScrubIndex` 语义、`PinSnapshotCommand`）、`app/Ecosim.Core/TimelineModel.cs`

- [ ] **Step 1: 写失败测试**：`TimelineModel` 层——`live 状态 Index == -1 且 MaximumIndex 变化不产生"回放中"副作用`；`ScrubIndex` 在 live 时读到 0 而不是上一帧索引；`PinSnapshot` 让 `Count` 加一且能 `Restore` 回去。
- [ ] **Step 2: 运行确认失败**
- [ ] **Step 3: 实现**：滑块 `Value` 绑到"live 时为 0、回放时为 Index"的单向可读值，避免 `Maximum` 变化回填；`Watermark` → `PlaceholderText`；`Ctrl+K` 钉住当前帧并在事件行给出反馈。
- [ ] **Step 4: 运行确认通过 + 截图核对滑块停靠**
- [ ] **Step 5: 提交** — `fix(app): scrubber resting position, placeholder text and pin shortcut`

### Task 5: 出口验证与文档收口

**Files:**
- Create: `docs/m4-checklist.md`
- Modify: `README.md`（进度到 M4 完成、存档格式说明、截图）、`docs/architecture.md`（cew 与增量直方图）、`CLAUDE.md`（新增边界规则：面板不得全表扫）

- [ ] **Step 1: 全套回归** — `scripts/build.sh && scripts/test.sh`
- [ ] **Step 2: 跑分核对** — Life 与生态混合网四个尺寸，写进 `docs/building.md`；生态 2048² 若跌破 20 代/秒，按 M3 的方式把增量开销调回去而不是记为取舍
- [ ] **Step 3: 目视清单** — 存 `.cew` → 改尺寸 → 读回（应报尺寸不符）；生态模式导出 RLE 的有损提示；`Ctrl+K` 钉帧后拖时间轴能回到那一帧
- [ ] **Step 4: 提交** — `docs: record M4 checklist and close out the milestone plan`
