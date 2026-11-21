# Cell Ecosim — M3 可视化与分析 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让生态"看得见、查得到、回得去"：能量/resource/年龄热力叠加层、种群数量-时间折线、能量直方图、native 产生并托管拉取的灭绝/激增事件日志、快照环形缓冲与时间轴 scrubber 回放。

**Architecture:** 全部状态仍在 native：调色盘尾部追加 `layer` 字段（附录 A.1 的"只增不改"），历史与事件是引擎内部的两个环形缓冲（每代样本 / 事件队列），快照是压缩后的全状态环（指数抽取）。托管侧只做两件事：按索引拉数据、用 `DrawingContext` 画图。UI 重绘继续走快照渲染路径，不跟 step 抢锁。

**Tech Stack:** 同 M1/M2：C++20 / MinGW / CMake / Avalonia 12.1.6 / net8.0 / xUnit / CTest。

**Spec:** `docs/superpowers/specs/2026-11-21-cell-ecosim-design.md` §11.2 界面、§13 M3、附录 A.1（palette 追加字段）、A.3（快照归属与能量语义）、A.5（无状态 RNG）；前置：`...-m0-m1.md`（已交付）、`...-m2-ecosystem.md`（已交付，见 `docs/m2-checklist.md`）。

## Global Constraints

- 继承 M1 计划的全部 Global Constraints（C++20、net8.0、pack4、`struct_size` 首字段、只增不改、中文 UI 文案、英文注释与提交信息、`scripts/*.sh` 入口）。
- 新增跨边界 struct 一律尾部追加，且 `ECOSIM_ABI_VERSION` 保持 1（既有语义未变）；`struct_size` 必须能拒绝旧客户端。
- 引擎内部不得为了可视化引入新的分配：历史/事件/快照三个环都在 `world_create` 时定容量，step 期间只写不分配。
- 可视化数据必须与线程数无关（`threads=1` vs `auto` 采样序列逐值相等）。
- 任何 UI 侧新增的每帧开销都要有预算：一帧的托管工作量（渲染 + 采样 + 画图）目标 ≤ 4 ms @512²。

## Review Focus

- **热力层的空胞语义**：能量热力下空格必须画成背景而不是"0 能量活细胞"，否则看不出谁活着。
- **历史环的回绕**：跨过环尾时索引取模，UI 不能画出跨代的直线；`history_count` 在未填满时是实际条数而不是容量。
- **事件不能刷屏**：激增判定要有滞回（hysteresis），否则种群在阈值附近抖动时每代都出一条事件。
- **快照 restore 的能量语义**：从只含 species 的旧数据恢复时按物种 `initial_energy` 重播（附录 A.3），restore 后不能一代全灭。
- **回放期间时钟必须停**：scrubber 拖动时若时钟还在跑，restore 会立刻被下一步覆盖；UI 要显式暂停并在离开回放时提示状态。

---

## 新增 ABI 面（跨任务共用契约）

```c
/* 追加到 EcosimPalette 尾部（旧客户端不传即 layer 0） */
int32_t layer;   /* 0=物种 1=能量 2=resource 3=年龄 */

/* 历史（每代一条样本，滑动窗口） */
typedef struct EcosimHistorySample {
  int32_t struct_size; uint64_t generation; int32_t alive_count;
  int32_t species_count[ECOSIM_MAX_SPECIES];
} EcosimHistorySample;                                  /* id 9  */
int32_t ecosim_history_count(const EcosimWorld*);
int32_t ecosim_history_get(const EcosimWorld*, int32_t index, EcosimHistorySample* out);

/* 事件（拉取式，避免 native 回调进托管） */
enum { ECOSIM_EVENT_EXTINCTION = 0, ECOSIM_EVENT_OUTBREAK = 1, ECOSIM_EVENT_APPEARANCE = 2 };
typedef struct EcosimEvent {
  int32_t struct_size; uint64_t generation; int32_t kind;
  int32_t species; int32_t count;
} EcosimEvent;                                          /* id 10 */
int32_t ecosim_event_poll(EcosimWorld*, EcosimEvent* out, int32_t cap, int32_t* out_count);

/* 快照（native 拥有唯一存储，指数抽取） */
int32_t ecosim_snapshot_count(const EcosimWorld*);
int32_t ecosim_snapshot_info(const EcosimWorld*, int32_t index, uint64_t* out_generation);
int32_t ecosim_snapshot_capture(EcosimWorld*);                       /* 手动钉住 */
int32_t ecosim_snapshot_restore(EcosimWorld*, int32_t index);        /* 0 = 最旧 */
```

---

### Task 1: 渲染叠加层（物种 / 能量 / resource / 年龄）

**Files:**
- Modify: `native/include/ecosim_abi.h`（`EcosimPalette` 尾部追加 `int32_t layer`）、`native/src/core/render.h/.cpp`、`native/tests/test_render.cpp`、`native/src/abi/ecosim_abi.cpp`（struct_size 前缀容忍）
- Modify: `app/Ecosim.Core/Abi/EcosimPaletteDto.cs`、`Rendering/Palette.cs`、`app/Ecosim.App/ViewModels/CanvasViewModel.cs`、`MainWindow.axaml`

**Interfaces:**
- Consumes: `species / energy / resource / age` 四个平面、`EcosimPalette.cell_bgra[16]`。
- Produces: `enum EcosimRenderLayer { SPECIES=0, ENERGY=1, RESOURCE=2, AGE=3 }`；
  `int32_t render::species_to_bgra(...)` 增加 layer 参数（由 palette 携带）；
  C# `Palette.WithLayer(RenderLayer)` 返回新 Palette；`CanvasViewModel.Layer` 属性 + 四个 RadioButton。

- [ ] **Step 1: 写失败测试**（`test_render.cpp` 追加）
  - `old_palette_struct_size_still_renders_species_layer`：`struct_size = 68`（无 layer 字段）→ 结果与 layer=0 一致
  - `energy_layer_maps_initial_energy_to_the_top_of_the_ramp`：species 1（80/240）→ 归一化 t≈0.33 → 断言落在预定义的色带分段里
  - `energy_layer_draws_empty_cells_as_background`：空格 → `cell_bgra[0]`
  - `resource_layer_ignores_energy_and_uses_255_scale`
  - `age_layer_saturates_at_its_own_max`
- [ ] **Step 2: 运行确认失败** — `bash scripts/build-native.sh`
- [ ] **Step 3: 实现**：`heat_ramp(double t)`（4 段线性插值，`constexpr` 表），layer 分派；托管 DTO 追加字段与 `RenderLayer` 枚举；UI 侧栏加"叠加层"四选一。
- [ ] **Step 4: 运行确认通过** — 两套测试全绿 + 截图（能量热力下能看到捕食者团块偏亮）
- [ ] **Step 5: 提交** — `feat: add render layers for energy, resource and age`

### Task 2: 历史环形缓冲与种群折线图

**Files:**
- Create: `native/src/core/history.h/.cpp`, `native/tests/test_history.cpp`, `app/Ecosim.App/Controls/PopulationChart.cs`
- Modify: `native/include/ecosim_abi.h`, `native/src/abi/ecosim_abi.cpp`, `native/src/core/world.h/.cpp`, `native/CMakeLists.txt`, `app/Ecosim.Core/*`（`HistorySeries`）, `MainWindow.axaml`

**Interfaces:**
- Produces: `class HistoryRing { explicit HistoryRing(int32_t capacity); void push(uint64_t gen, const Planes&, int32_t w, int32_t h); int32_t size() const; EcosimHistorySample at(int32_t i) const; }`，容量 `ECOSIM_HISTORY_CAPACITY = 4096`；
  C# `IReadOnlyList<HistorySample> World.History()`（一次拉全量，UI 每帧只拉尾部）；
  `PopulationChart : Control` 画三条物种曲线 + 坐标刻度。

- [ ] **Step 1: 写失败测试**（`test_history.cpp`）
  - `history_is_empty_on_a_fresh_world` → `ecosim_history_count == 0`
  - `one_sample_per_generation_in_order`：step 50 → count 50，`at(0).generation == 1`，`at(49).generation == 50`
  - `ring_wraps_and_keeps_the_newest`：step 4200（容量 4096 时用 `ECOSIM_HISTORY_CAPACITY` 的测试专用小值？改为：容量常量不变，测试用 `HistoryRing(8)` 直接单测 C++ 类）→ 只剩最新 8 代且顺序正确
  - `history_matches_stats_for_every_sample`：逐条与同时刻 `stats` 一致
  - `samples_do_not_depend_on_thread_count`
- [ ] **Step 2: 运行确认失败**
- [ ] **Step 3: 实现**：`World::step` 每代 push（增量统计：管线里已经数过一次，避免再扫全表——`stats` 改为读缓存）；ABI 两个导出；C# `HistorySample`；`PopulationChart.Render`。
- [ ] **Step 4: 运行确认通过 + 截图**
- [ ] **Step 5: 提交** — `feat: add per-generation history ring and population chart`

### Task 3: 事件队列（灭绝 / 激增 / 再现）与事件日志

**Files:**
- Create: `native/src/core/events.h/.cpp`, `native/tests/test_events.cpp`
- Modify: `native/include/ecosim_abi.h`, `native/src/abi/ecosim_abi.cpp`, `native/src/core/world.cpp`, `app/Ecosim.Core/Events.cs`, `app/Ecosim.App/ViewModels/InspectorViewModel.cs`, `MainWindow.axaml`

**Interfaces:**
- Produces: `class EventQueue { int32_t poll(EcosimEvent*, int32_t cap, int32_t* out_count); void observe(uint64_t gen, const int32_t counts[16], int32_t cells); }`，容量 256，满了丢最旧；
  激增判定：`count > outbreak_threshold`（= 总格数 × 5%）且上一代不满足；灭绝判定：`count == 0` 且上一代 > 0；再现反之（滞回：激增需回落到 4% 以下才重新武装）。
  C# `World.PollEvents()` → `IReadOnlyList<SimEvent>`；`InspectorViewModel.Events` 绑定到 ListBox（新事件在上）。

- [ ] **Step 1: 写失败测试**
  - `no_events_before_the_first_step`
  - `extinction_is_reported_once`：把某物种饿死 → 恰好一条 EXTINCTION，之后继续 step 不再重复
  - `outbreak_needs_to_cross_and_then_reset_below_the_hysteresis`：人造种群跨过 5% → 一条；在阈值附近抖动 → 不产生第二条；跌到 4% 以下再跨 → 第二条
  - `queue_overflow_drops_the_oldest_not_the_newest`
  - `poll_clears_the_queue`
- [ ] **Step 2: 运行确认失败**
- [ ] **Step 3: 实现**（observe 在 `World::step` 每代 push 之后调用；Life 模式下 species 只有 1，同样能报灭绝）
- [ ] **Step 4: 运行确认通过 + 截图**（事件日志出现"G842 虫 局部灭绝"样式）
- [ ] **Step 5: 提交** — `feat: add extinction and outbreak events with hysteresis`

### Task 4: 快照环形缓冲（native 拥有唯一存储）

**Files:**
- Create: `native/src/core/snapshots.h/.cpp`, `native/tests/test_snapshots.cpp`
- Modify: `native/include/ecosim_abi.h`, `native/src/abi/ecosim_abi.cpp`, `native/src/core/world.h/.cpp`, `native/CMakeLists.txt`

**Interfaces:**
- Produces: `class SnapshotRing { explicit SnapshotRing(int32_t slots = 64); void maybe_capture(uint64_t gen, const Grid&); int32_t capture(const Grid&); int32_t restore(Grid&, int32_t index) const; int32_t count() const; uint64_t generation_at(int32_t) const; }`
  存储：`species + energy + resource` 的 run-length 压缩（age 不存，恢复时置 0）；
  抽取：最近 16 帧逐代，其后步长翻倍（1→2→4→8…），槽位满时覆盖最旧；
  `restore(0)` = 最旧，`restore(count-1)` = 最新。
- ABI 四个导出如上。

- [ ] **Step 1: 写失败测试**
  - `no_snapshots_before_capture`
  - `capture_then_restore_is_lossless_for_species_energy_resource`（age 允许归零，断言 age==0）
  - `decimation_widens_the_stride`：step 40 代后 count ≤ 16 + 24/2 + … 且代号单调递增
  - `restoring_an_old_snapshot_then_stepping_forward_reproduces_the_later_board`：
    restore 到第 k 帧 → 步进 (gen_k+1 与当前之间的代数) → 与不 restore 直接跑的哈希一致（**这条是回放的真正验收**）
  - `restore_resets_age_to_zero_and_keeps_energy`（附录 A.3）
  - `ring_overflow_keeps_the_newest`
- [ ] **Step 2: 运行确认失败**
- [ ] **Step 3: 实现**
- [ ] **Step 4: 运行确认通过**
- [ ] **Step 5: 提交** — `feat(native): add decimating snapshot ring with lossless restore`

### Task 5: 时间轴 scrubber 与回放

**Files:**
- Create: `app/Ecosim.Core/TimelineModel.cs`, `app/Ecosim.Tests/TimelineTests.cs`
- Modify: `app/Ecosim.App/ViewModels/MainViewModel.cs`（Timeline 属性、Enter/Exit 回放）、`MainWindow.axaml`（底部 scrubber 行）

**Interfaces:**
- Produces: `TimelineModel { int Count; int Index; ulong GenerationAt(int); void Refresh(); bool Restore(int index); }`；
  UI：`Slider` 0..Count-1 + "代 N" 标签 + `回放/退出回放` 按钮；进入回放自动暂停时钟，退出时回到最新帧并提示。

- [ ] **Step 1: 写失败测试**：`TimelineTests`（Core 层，不起窗口）：Count 跟随快照数；Index 越界钳制；Restore 后 `World.Stats().Generation` == 该帧代号；退出回放后重新捕获会让 Count 增长；**拖动 scrubber 期间没有 step 发生**（用 World.Generation 不变断言）。
- [ ] **Step 2: 运行确认失败**
- [ ] **Step 3: 实现**
- [ ] **Step 4: 运行确认通过 + 截图**（scrubber 拖到中段，画面是历史某一帧）
- [ ] **Step 5: 提交** — `feat(app): add timeline scrubber with pause-on-review`

### Task 6: 能量直方图与 M3 出口验证

**Files:**
- Create: `app/Ecosim.App/Controls/EnergyHistogram.cs`, `docs/m3-checklist.md`
- Modify: `MainWindow.axaml`（右栏或底栏挂直方图）、`app/Ecosim.Core/World.cs`（`EnergyHistogram(int buckets)` 由快照在托管侧算，零 native 改动）

- [ ] **Step 1: 写失败测试**：`World.EnergyHistogram` 在已知小盘面上桶和 == Alive、空盘全 0、桶数钳制。
- [ ] **Step 2: 运行确认失败**
- [ ] **Step 3: 实现**（直方图从已有的 `SpeciesSnapshot` 算，不新增 native 扫描）
- [ ] **Step 4: 全套回归** — `scripts/test.sh` + `scripts/bench.sh`；把每帧开销实测写进 `docs/m3-checklist.md`
- [ ] **Step 5: 目视清单核对 + 提交** — `docs: record M3 checklist and visual verification`

## M4（下一份计划）

`cew` 二进制存档、矢量降级缩放、快捷键收尾、并行度自动选择、每帧 8 ms 预算调优。
