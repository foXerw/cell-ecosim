# Cell Ecosim — M2 多物种生态 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把引擎从"生命游戏"升级成"多物种细胞生态"：细胞带能量/年龄/环境养分，六阶段管线（摄食→代谢→繁殖→扩散→再生），参数表驱动 UI 自动生成滑杆；Conway 规则保留为同一引擎的退化模式。

**Architecture:** 管线仍在 native（`src/rules/`），每阶段读 `cur` 写 `nxt`；参数走新增的 `EcosimParams` + `EcosimParamDesc`（描述表 → C# 自动生成滑杆，加一个 C++ 参数不需要改 C#）；模式切换按附录 A.1 走"重建 world + RLE 迁移"，不做热切。

**Tech Stack:** 同 M1：C++20 / MinGW / CMake / Avalonia 12.1.6 / net8.0 / xUnit / CTest。

**Spec:** `docs/superpowers/specs/2026-11-21-cell-ecosim-design.md` §8.2/§8.3/§8.4、附录 A.1/A.5；前置计划 `2026-11-21-cell-ecosim-m0-m1.md`（M1 已交付，见 `docs/m1-checklist.md`）。

## Global Constraints

- 继承 M1 计划的全部 Global Constraints（C++20、net8.0、pack4、struct_size、只增不改、中文 UI 文案、英文注释与提交信息）。
- 随机必须继续用 `util/rng.h` 的无状态派生（附录 A.5.2）：繁殖/扩散/突变的随机数取
  `rng::draw(seed, generation, index * k + stage)`，**stage 常数互不相同**，保证同代同格可复现且线程无关。
- 六阶段顺序固定：sense → feed → metabolise → reproduce → disperse → regrow；
  每阶段只写自己负责的字段，跨阶段冲突用"sense 阶段产出候选表、后续阶段按固定顺序 apply"解决。
- 单线程 vs 多线程结果逐位一致是硬性质，每个新阶段都要有对应的一致性测试。
- 参数描述表是 UI 的唯一参数来源：`EcosimParamDesc` 带 id/中文名/范围/步长/分组。

## Review Focus

- **能量守恒与爆炸**：摄食效率 < 1、繁殖成本 < 1、代谢 > 0 三者共同决定系统不指数爆炸；
  期望"固定种子 + 固定参数下 2000 代内总能量有上界"。测试钉住。
- **灭绝不是崩溃**：某物种清零后管线继续跑、不除零、不 NaN、stats 里计数为 0。
- **参数热改的生效时机**：`params_set` 在 step 之间生效；一次 step 中途改参数不允许（锁保证）。
- **模式切换的数据迁移**：Life → 生态后能量按物种 `initial_energy` 初始化（不是 0，避免读档即全灭，附录 A.3）。
- **描述表与 DTO 的一致性**：`param_count` × `param_desc` 与 `EcosimParams` 的字段数必须一致，
  由一条"遍历描述表读写每个参数"的往返测试钉住。

---

### Task 1: 物种表与能量平面接通

**Files:**
- Create: `native/src/core/species.h`, `native/src/core/species.cpp`, `native/tests/test_species.cpp`
- Modify: `native/src/core/world.cpp`（randomize/clear 写入 initial_energy）、`native/CMakeLists.txt`

**Interfaces:**
- Produces: `struct SpeciesDef { int32_t id; const char* name; uint16_t max_energy; uint16_t initial_energy; uint16_t metabolism; uint16_t repro_threshold; uint8_t feed_efficiency_permille; uint8_t mobility_permille; uint8_t mutation_permille; }`、
  `const SpeciesDef* species_table(int32_t* out_count)`、`int32_t species_count()`。
  预置：1 藻 / 2 虫 / 3 兽，数值取设计 §8.2 表。

- [ ] **Step 1: 写失败测试**：表长度 3、id 连续、`initial_energy <= max_energy`、代谢 > 0、效率 < 1000‰。
- [ ] **Step 2: 运行确认失败**。
- [ ] **Step 3: 实现表**；`randomize`/`clear` 对活格写 `initial_energy`、死格写 0。
- [ ] **Step 4: 运行确认通过**（native 全绿）。
- [ ] **Step 5: 提交** `feat(native): add species table and wire energy into randomize/clear`。

### Task 2: sense 阶段（分物种邻域计数 + resource 合计）

**Files:**
- Create: `native/src/rules/pipeline.h`, `native/src/rules/sense.cpp`, `native/tests/test_sense.cpp`
- Modify: `native/src/core/world.h/.cpp`（step 在 ECOSYSTEM 模式下走管线）、`native/CMakeLists.txt`

**Interfaces:**
- Produces: `struct SenseField { std::vector<uint16_t> per_species[ECOSIM_MAX_SPECIES]; std::vector<uint16_t> resource_sum; }`、
  `void sense(const Planes& in, int32_t w, int32_t h, int32_t topology, int32_t y0, int32_t y1, SenseField& out)`（带区间版，供行带并行）。
- `World::step` 在 `ECOSIM_MODE_ECOSYSTEM` 下调用管线；`world_create` 放开对该模式的拒绝（M1 里 validate 拒绝它）。

- [ ] **Step 1: 写失败测试**：手摆 3×3 混合物种，逐格断言计数；torus 与 bounded 各一例；带区间版与整格版逐位一致。
- [ ] **Step 2: 运行确认失败**。
- [ ] **Step 3: 实现**（复用 kernel_fast 的三行滑窗思路，按物种累加）。
- [ ] **Step 4: 运行确认通过**。
- [ ] **Step 5: 提交** `feat(native): add sense stage with per-species neighbour counts`。

### Task 3: feed / metabolise / reproduce / disperse / regrow

**Files:**
- Create: `native/src/rules/{feed,metabolise,reproduce,disperse,regrow}.cpp`, `native/tests/test_pipeline.cpp`
- Modify: `native/src/rules/pipeline.h`、`native/src/core/world.cpp`、`native/CMakeLists.txt`

**Interfaces:**
- Produces: `void run_pipeline(World&, const EcosimParams&)`（内部按固定顺序调五阶段 + sense）；
  每阶段签名统一 `(const Planes& in, Planes& out, const SenseField&, const SpeciesDef*, const EcosimParams&, int32_t w,h, y0,y1, uint64_t generation, uint64_t seed)`。

- [ ] **Step 1: 写失败测试**（全部手算小网格）：
  虫邻接藻 → 虫能量 += 藻能量×效率、藻死亡并留下 resource；
  能量 ≤0 → 死亡且 resource += 尸体量；
  能量 ≥ 阈值 → 空邻居出现子代、亲代扣成本；
  扩散按 mobility 交换空位且一格只搬一次；
  resource 达阈值按 fertility 长出藻。
- [ ] **Step 2: 运行确认失败**。
- [ ] **Step 3: 实现五阶段**（随机一律 `rng::draw(seed, generation, index*k+stage)`）。
- [ ] **Step 4: 运行确认通过**。
- [ ] **Step 5: 提交** `feat(native): implement the five ecosystem stages`。

### Task 4: 参数表 EcosimParams + 描述表 + 热改

**Files:**
- Modify: `native/include/ecosim_abi.h`（追加 `EcosimParams`、`EcosimParamDesc`、`ecosim_param_count/desc/params_get/params_set`）、`native/src/abi/ecosim_abi.cpp`、`native/src/core/params.h/.cpp`、`native/tests/test_params.cpp`
- Modify: `app/Ecosim.Core/Abi/*Dto.cs`、`Interop/NativeMethods.cs`、`World.cs`

**Interfaces:**
- 参数（首批）：`light_permille`、`fertility_permille`、每物种的 `metabolism_scale_permille` 与 `repro_scale_permille`（描述表里按物种展开成独立条目）。
- 托管：`World.Params` get/set、`IReadOnlyList<ParamDesc> ParamDescriptions`。

- [ ] **Step 1: 写失败测试**：描述表遍历往返（每个参数 set 边界值再 get 相等）；越界 set 返回 ERR_RANGE；struct_size 旧客户端（小 struct）仍可 get。
- [ ] **Step 2: 运行确认失败**。
- [ ] **Step 3: 实现**（native 表 + ABI + 托管 DTO/门面）。
- [ ] **Step 4: 运行确认通过**（两套全绿）。
- [ ] **Step 5: 提交** `feat: add parameter table with description-driven ABI`。

### Task 5: 自动参数面板（UI 由描述表生成）

**Files:**
- Create: `app/Ecosim.App/ViewModels/ParamsViewModel.cs`, `app/Ecosim.App/Views/ParamsPanel.axaml(.cs)`
- Modify: `MainWindow.axaml`（侧栏挂 ParamsPanel）、`MainViewModel.cs`

**Interfaces:**
- `ParamSliderViewModel { Id, Label, Min, Max, Step, Value }`；面板 `ItemsControl` 绑定
  `ParamsViewModel.Sliders`；Value 变更 → `World.Params` set（在 UI 线程，锁内生效于下一次 step）。

- [ ] **Step 1: 写失败测试**（xUnit，ViewModel 层）：描述表条数 == 滑杆条数；改滑杆 → `World.Params` 对应字段变化；越界钳制。
- [ ] **Step 2: 运行确认失败**。
- [ ] **Step 3: 实现面板**（中文标签来自描述表）。
- [ ] **Step 4: 运行确认通过 + 目视截图**。
- [ ] **Step 5: 提交** `feat(app): generate the parameter panel from the description table`。

### Task 6: 生态模式入口、模式切换与出口验证

**Files:**
- Modify: `MainViewModel`（模式下拉：Life / 生态；切换走 RLE 迁移）、`MainWindow.axaml`
- Create: `native/tests/test_ecosystem.cpp`、`docs/m2-checklist.md`

- [ ] **Step 1: 写失败测试**：固定种子三物种随机汤，2000 代内出现 ≥2 次"捕食者峰值滞后于猎物峰值"的振荡
  （用 stats 的物种计数序列断言峰的位置关系）；某物种灭绝后继续跑 500 代不崩溃；总能量有上界。
- [ ] **Step 2: 运行确认失败**。
- [ ] **Step 3: 实现**：生态模式的 randomize（三物种分层密度）、模式切换迁移（能量按 initial_energy）、UI 下拉。
- [ ] **Step 4: 运行确认通过 + 目视截图（能看到藻毯被虫吃出空洞）**。
- [ ] **Step 5: 提交** `feat: wire ecosystem mode end to end with oscillation test`，并写 `docs/m2-checklist.md`。
