# Cell Ecosim 设计文档

日期：2026-11-21 ｜ 仓库：https://github.com/foXerw/cell-ecosim ｜ 状态：待批准

## 1. 意图与动机

用 C++ 和 C# 重做一次细胞自动机，一边复习两门语言：C++ 做计算后端，C# + Avalonia 做桌面 UI。
之前那版 Python 生命游戏极其简陋，没有参考价值，这次的目标是**做丰富**：不只"能演化"，而是"能画、能调、能看、能存、能回放"。

成功的样子（按重要性排序）：

1. 两门语言各自真正想吃透的部分都被练到。
   - C++：C ABI 边界设计、内存布局（SoA / 缓存友好）、`std::jthread` 并行、CMake 构建、原生单测。
   - C#：P/Invoke 与 `SafeHandle`、`Span<T>`/`Memory<T>`、类型系统与不可变 DTO、事件与 MVVM 绑定、`async` 时钟。
2. 玩起来有意思：画个滑翔鱼就看着它吃掉一整个藻类群落，或者调一个参数让生态系统从灭绝边缘翻回来。
3. 大网格不掉帧、不 OOM。

## 2. Goals

- **性能**：多物种生态规则下 2048×2048 ≥ 20 FPS；Life 规则下 1024×1024 ≥ 60 FPS（步进 ≤ 8 ms 预算）。
- **完整可玩的 UI**：绘制/擦除/物种笔刷、拖拽平移、滚轮缩放、播放/暂停/单步/变速、规则切换、参数滑杆面板、图案库（含标准 RLE 导入导出）、叠加热力可视化、种群曲线、快照时间轴回放、存档/读档。
- **边界是通用的**：同一套 C ABI 既能被 Avalonia 用，也能被一个 headless CLI 用（`ecosim-cli`：批量跑、跑分、无 UI 转换存档格式）。这条是"后端"这个词的实质检验。
- **有测试**：native 侧规则/引擎单测；托管侧通过 P/Invoke 真调 DLL 的集成测试（不是 mock）。
- **一键可用**：`./build.sh`（或 `build.ps1`）配置+编译+测试，`./run.sh` 起 UI。

## 3. Non-goals

- 不做网络、多人、服务端部署。
- 不做 GPU 计算（compute shader / OpenCL）；本期只做 CPU，架构上预留"step 由谁执行"可替换。
- 不做跨平台打包。代码保持可移植（不写 Windows-only 逻辑在引擎里，除了 DLL 导出宏），但只在 Windows 上验证。
- 不做插件/脚本化规则引擎（不内嵌 Lua、不加载用户编译的规则 so）。规则是"数据表 + 编译期内建物种"。
- 不追求生物学正确性。模型只要"看起来合理、参数手感可玩"。
- 不做移动端。

## 4. Anti-goals（技术上跑通了也算失败）

- UI 卡成幻灯片，或者大网格直接爆内存。
- C ABI 上贴满 STL / C++ 类型 / 异常，P/Invoke 签名靠猜靠试。
- 模拟逻辑漏进 Avalonia 控件和事件处理器里（UI 只做呈现与输入转发）。
- 只有 Life 一条硬编码路径：加一个新物种或新参数要改五六个文件、两边同步。
- 没有一行测试，只能靠肉眼看网格对不对。

## 5. 约束（已在机器上验证）

| 项 | 事实 |
|---|---|
| OS / shell | Windows 11 (22621)，bash (Git Bash / MSYS2 环境) |
| C++ | 只有 MinGW `g++` 16.1（ucrt64），**无 MSVC、无 clang** |
| 构建 | CMake 4.4；`-shared` 出 DLL 已验证可行，`std::jthread`(C++20) 已验证可用；libstdc++/libgcc/libwinpthread **静态链接**，产物不依赖 MSYS2 运行时 |
| .NET | SDK 8.0.303 与 10.0.203 均已安装 |
| Avalonia | NuGet 最新 12.1.6（`Avalonia`、`Avalonia.Desktop`、`Avalonia.Themes.Fluent`）；NuGet 源被重定向到 nuget.azure.cn 镜像，可达 |
| 语言选择 | C++20；C# 目标框架 `net8.0`（两个 SDK 都能建，不依赖 .NET 10 独有特性） |
| 其他 | Avalonia 项目模板本机未安装，csproj + AXAML 手写（不依赖 `dotnet new`） |

## 6. 方案与取舍

架构路线已选定 **A**（由用户在三选项中确认）：

- **A（选定）进程内 P/Invoke 原生 DLL**：C++ 编译出纯 C ABI 的 `ecosim.dll`，不透明句柄 + `extern "C"` 导出；C# 侧一个托管安全壳程序集，Avalonia 直接把 C++ 算好的 BGRA 帧 blit 进 `WriteableBitmap`。零序列化、零进程间拷贝，练的是 C++ 核心功力。代价：调试要 gdb + dotnet 两套；跨边界 bug 更难定位。
- **B（弃）独立后端进程 + socket/共享内存**：更"后端"，但 60fps 下帧传输协议与同步要吃掉一半以上工时，反而练不深引擎。适合"模拟即服务"，不适合桌面沙盒。
- **C（弃）C++/CLI 混合程序集**：本机构建器是 MinGW，做不了 MSVC 专属的 C++/CLI；技能可迁移性也差。工具链直接排除。

模拟模型的取向（我的决定，理由：用户要求"丰富"）：引擎是**多物种细胞生态**，Conway 生命游戏是它的一个退化预设（单物种 + B/S 数字规则表），两者共用邻域计数、并行、渲染、图案库。这样"生命游戏播放器"和"生态沙盒"不必二选一，也不必重写引擎。

## 7. 架构

```
 native/  (C++)
   ┌────────────────────────────────┐
   │ ecosim engine (core, 无 UI 依赖) │
   └───────────┬────────────────────┘
        ecosim_abi.h  (纯 C ABI)
               │  编译成 ecosim.dll
      ┌────────┴─────────┐
      │                  │
  app/Ecosim.Core    tools/ecosim-cli   ← 同一 ABI 的第二个消费者（headless）
      │  (P/Invoke 托管壳，无 UI 依赖)
      │
  app/Ecosim.App  (Avalonia，MVVM)
```

分层规则（违反即返工）：

- `Ecosim.Core` 是**唯一**跨边界的 C# 程序集；`Ecosim.App` 看不到任何 `DllImport`。
- native 引擎里没有 UI 概念、不读文件路径（I/O 在调用方）、不假设自己是 DLL 还是 exe 的一部分。
- 帧缓冲由**调用方提供**指针传入，native 写入 → 零拷贝到 `WriteableBitmap`。
- native 不拥有时钟。步进节奏由 C# 的 `SimClock` 决定；native 每次被调用就把 `n_steps` 跑完。跨线程状态只在一侧存在，避免竞态。

## 8. 数据模型

### 8.1 网格与布局

- 网格 `W×H`，拓扑可选：`torus`（默认，环形）/ `bounded`（边缘视为永久死亡）。
- **SoA（Structure of Arrays）**，每格 4 个平面：
  - `uint8 species`  （0 = 空位，其余是物种表下标）
  - `uint16 energy`  （0..65535，上限来自物种参数）
  - `uint16 age`
  - `uint8 resource` （环境养分/残骸，0..255）
  - 4 字节/格；2048×2048 ≈ 16 MB，双缓冲 32 MB。
- 为什么 SoA：邻域统计按 `species` 平面连续扫描（这是最热循环），`resource` 独立平面让"衰减/再生"阶段不与主状态争缓存行；分带并行时每线程按同一行区间访问各平面，无伪共享。
- 双缓冲 ping-pong：每个阶段读 `cur` 写 `nxt`；阶段之间整块交换平面指针（不 memcpy）。

### 8.2 预置物种

| id | 名称 | 角色 | 关键参数（默认） |
|---|---|---|---|
| 0 | 空位 | — | — |
| 1 |藻 / Alga | producer：靠 `resource` 自养增长 | 光合率 0.35，代谢 1，繁殖阈值 60，扩散概率 0 |
| 2 | 虫 / Grazer | grazer：吃 alga，会动 | 摄食效率 0.5，代谢 3，繁殖阈值 90，扩散概率 0.25，突变率 0.002 |
| 3 | 兽 / Hunter | predator：吃 grazer，跑得快、生得少 | 摄食效率 0.7，代谢 5，繁殖阈值 150，扩散概率 0.35，突变率 0.001 |
| 4 | *(已废弃为物种 id)* | Life 是**模式** `ECOSIM_MODE_BIRSURV`，不是物种；该模式下活格一律 `species = 1`（见附录 A.7） | 由 `mode` 走 B/S 表，忽略能量/物种阶段 |

物种表数据驱动：一个物种 = `EcosimSpeciesDef` 里的一行（颜色、能量上限、各阶段参数）。加第 4 个物种只改 native 的一张表 + 参数描述表，UI 与 ABI 不动。

### 8.3 规则模式

`EcosimConfig.mode`：
- `ECOSIM_MODE_ECOSYSTEM` — 走完整六阶段管线。
- `ECOSIM_MODE_BIRSURV` — 只跑 `sense` + 一张 `uint8 birth_mask[9]` / `survive_mask[9]`（Life = 3/2,3；HighLife = 3,6/2,3；可自定义）。能量/年龄/资源仍在，但不参与规则（便于以后把 Life 染色成生态）。

两种模式共用：邻域计数内核、并行分带、渲染、绘制、图案库、RLE、统计骨架。

### 8.4 六阶段管线（固定顺序；每阶段 读 cur / 写 nxt）

1. **sense** — 对每格统计 8 邻域：按物种分桶的邻居计数、`resource` 合计。内核用 3 行滑窗 + bitmask `popcount` 查表，不写分支。
2. **feed** — predator 吃 grazer、grazer 吃 alga。每个捕食者在"本格 + 邻域"内选一个猎物：得分 = 猎物能量 × 摄食效率 − 距离惩罚。转移 `min(猎物能量, 胃口)`，猎物死亡即留下 `resource`。
3. **metabolise** — `age += 1`，`energy -= 代谢率 + 移动惩罚`；`energy <= 0` → 死亡，`species = 0`，`resource += 尸体量`。
4. **reproduce** — `energy >= 繁殖阈值` 时，在空邻居里挑最优位（`resource` 高 + 物种拥挤度低），子代能量 = 亲代 × `cost`，亲代扣除同等能量；按 `mutation_rate` 小概率改变子代物种或扰动一个参数。近亲/拥挤惩罚用"同物种邻居数"实现，避免单物种爆炸。
5. **disperse** — 按物种 `mobility` 概率与相邻空位交换（在 `nxt` 上操作，保证一格只搬一次）。
6. **regrow** — `resource` 达到阈值时自发长出 producer（概率受全局 `fertility`/`light` 参数控制）。

`step(n)` 就是把这个管线跑 n 次，一次跑完不返回中间状态（CLI/UI 若需要逐帧，把 n 设成 1）。

## 9. C++ 侧：内核与性能

- 并行：行带（row-band）切分 + `std::jthread` 池；池在 `world_create` 时建立（线程数 = `min(hardware_concurrency, 行数/最小带高)`），step 期间复用，避免每步起线程。带高取 64 行的整数倍（邻域 r=1 需要上下各 1 行 halo，用带边界重叠复制或环形三段读取处理）。
- 邻居扫描：`sense` 阶段把 8 邻居编码成 1-bit-per-species 的 bitmask 组合，用 256 项 popcount 查表；torus 用取模换成"行首尾复制 + 列 wrap 查表"，热循环里没有 `%`。
- 不做 intrinsics/SIMD 手写（可读性优先，本期靠内存布局与并行拿性能）；`CMakeLists.txt` 里预留 `-march=native` 开关，默认关闭以保证产物可移植。
- 帧预算：native 暴露 `ecosim_step` 的耗时统计，UI 显示"步进 X ms / 渲染 Y ms"，让性能可见、可归因。

## 10. ABI 设计

单一头文件 `native/include/ecosim_abi.h`，`extern "C"`，`__cdecl`，`ECOSIM_API` 宏处理 `dllexport/dllimport`。边界上只有：不透明指针、固定宽度整数、`struct`（带 `struct_size` 首字段）、函数指针（回调只在"日志/事件"一处使用，且本期先留空不实现回调，避免线程亲和问题）。

```c
// 生命周期与元信息
int32_t      ecosim_abi_version(void);            // 不匹配 → 托管侧直接抛
const char*  ecosim_last_error(void);              // 线程局部，返回 UTF-8 静态缓冲
EcosimWorld* ecosim_world_create(const EcosimConfig* cfg);
void         ecosim_world_destroy(EcosimWorld*);

// 推进与读取
int32_t  ecosim_world_step(EcosimWorld*, int32_t n_steps);
int32_t  ecosim_world_grid_view(const EcosimWorld*, EcosimGridView* out);   // 各平面指针 + 维度 + stride
int32_t  ecosim_stats(const EcosimWorld*, EcosimStats* out);

// 渲染：调用方给缓冲，native 按 palette 写 BGRA8
int32_t  ecosim_render_frame(const EcosimWorld*, uint8_t* bgra,
                             int32_t row_pitch, const EcosimPalette* pal);

// 输入
int32_t  ecosim_paint(EcosimWorld*, const EcosimPaintOp* ops, int32_t count);
int32_t  ecosim_clear(EcosimWorld*, uint8_t fill_species);
int32_t  ecosim_randomize(EcosimWorld*, const EcosimSeedConfig* cfg);

// 参数（热改，运行中生效）
int32_t  ecosim_param_count(void);
int32_t  ecosim_param_desc(int32_t index, EcosimParamDesc* out);  // id/名称/范围/步长/分组
int32_t  ecosim_params_get(const EcosimWorld*, EcosimParams* out);
int32_t  ecosim_params_set(EcosimWorld*, const EcosimParams*);

// 持久化（内容字符串进出，I/O 归调用方）
int32_t  ecosim_to_rle(const EcosimWorld*, char* buf, int32_t cap, int32_t* written);
int32_t  ecosim_from_rle(EcosimWorld*, const char* rle);
int32_t  ecosim_save_cew(const EcosimWorld*, uint8_t* buf, int32_t cap, int32_t* written);
int32_t  ecosim_load_cew(EcosimWorld*, const uint8_t* buf, int32_t len);

// 快照 / 回放（native 里的环形缓冲）
int32_t  ecosim_snapshot_capture(EcosimWorld*);
int32_t  ecosim_snapshot_restore(EcosimWorld*, int32_t offset);   // 0 = 最新
int32_t  ecosim_snapshot_count(const EcosimWorld*);
```

边界纪律（写进 CLAUDE.md 供后续遵守）：

- 所有跨边界 `struct`：`int32_t struct_size` 作为首字段，托管侧填 `Marshal.SizeOf<T>()`，native 侧校验并只读它认识的字段前缀 → ABI 可加字段不破坏旧客户端。
- 固定宽度整数、显式 `pack`（`#pragma pack(4)` 与 C# `[StructLayout(Pack=4)]` 一致）、`enum` 一律 `int32_t`。
- 不跨边界传：C++ 对象、STL、异常、引用、`bool`、`char` 之外的宽字符。
- 返回码统一 `ECOSIM_OK=0` / 负数错误码，配合 `ecosim_last_error()`；托管侧包成异常。
- 所有权：`*_view` / `*_get` 返回的指针是**借用**（下次 step 前有效），托管文档明确标注；缓冲区一律调用方分配。

## 11. C# 侧

### 11.1 `Ecosim.Core`（无 UI 依赖）

- `Interop/Unsafe.cs`：所有 `DllImport("ecosim")` 声明集中一处，私有静态。
- `NativeWorld : SafeHandle`：构造时校验 `ecosim_abi_version()`，`Dispose` 调 `world_destroy`。所有方法在句柄失效时抛 `ObjectDisposedException`。
- DTO：`WorldConfig` / `PaintOp` / `Palette` / `Stats` / `GridView` / `ParamDesc` 均为 `[StructLayout(LayoutKind.Sequential, Pack=4)]` 的 `struct`，与头文件逐字段对齐；每个 `struct` 有一条单元测试用 `Marshal.SizeOf` 断言字节布局（和 native 侧 `static_assert` 成对）。
- `FrameBuffer`：`byte[]`/`Memory<byte>` 复用缓冲，尺寸变化才重分配；提供 `Span<byte> AsSpan()` 给 UI 直接 `CopyTo` 位图后缓冲。
- `World`（托管门面）：`Step(int)`、`Paint(IReadOnlyList<BrushStroke>)`、`Params`、`Stats`、`TryGetFrame()`；把返回码转成异常（`EcosimException`）；不持有 UI 概念。
- `SimClock`：`Task` + `PeriodicTimer` 驱动的步进循环（后台线程），暴露 `Tick` 事件与 `Generation`；UI 用 `DispatcherTimer`/帧回调取最新帧。**C# 拥有节奏**。
- `PatternLibrary` / `Rle`：RLE 文本解析与生成（标准 Golly 兼容子集：`x = , y = , rule = B3/S23, bo$ab!`），图案以内嵌 + `data/patterns/*.rle` 外置两种来源加载（glider、lwss、Gosper 枪、pulsar、r-pentomino、随机汤、多物种"鱼群/藻毯"起手式）。
- `Timeline`：环形快照 + 抽取倍率（decimation），支持"倒着看"的 scrubber。

### 11.2 `Ecosim.App`（Avalonia 12 + Fluent 主题）

MVVM，手写极简 `ObservableObject` / `RelayCommand`（`INotifyPropertyChanged` + `CallerMemberName`），**不引入** CommunityToolkit.Mvvm —— 目的是练到 C# 事件与绑定这一层本身。（我的决定）

视图模型：`MainViewModel`（时钟、状态栏、命令）、`CanvasViewModel`（缩放/平移/绘制、命中转换）、`RulesViewModel`（模式切换、参数面板 VM 生成）、`InspectorViewModel`（统计、曲线、事件日志、时间轴）。

界面（这就是打开软件看到的东西）：

```
┌────────────────────────────────────────────────────────────────────────┐
│ ▶/⏸  ⏭单步  速度 ▓▓▓░ 5gen/s │ 模式: 生态 ▼ │ Gen 1,204 │ 43,217 个体 │ ⌨ F5 │
├───────────────┬────────────────────────────────────────────────────────┤
│ 工具          │                                                        │
│ ●画  ○擦  ○种 │          网格画布（滚轮缩放 1x–64x / 拖拽平移）           │
│ 笔刷 [1..32]  │          叠加层：物种着色 / 能量 / resource 热力         │
│ 物种 ▾ 藻虫兽 │          （悬停显示一格详情：物种·能量·年龄）             │
├───────────────┼────────────────────────────────────────────────────────┤
│ 参数 (自动)   │  种群数量–时间折线（最近 600 代，每种一条线）             │
│ 光合率 ▓▓▓░   │  ─────────────────────────────  │ 事件日志              │
│ 代谢   ▓▓░░   │  直方图：能量分布 / 年龄分布      │  "G842 grazer 局部灭绝"│
│ 繁殖阈值 ▓▓▓ │                                   │  "G1103 hunter 激增"   │
├───────────────┼────────────────────────────────────────────────────────┤
│ 图案 ▾        │  时间轴 scrubber  ◀────●──────▶  快照 24/64  ⏵ 回放      │
│ 存档/读档     │  状态：步进 5.2 ms · 渲染 1.1 ms · 4 线程                │
└───────────────┴────────────────────────────────────────────────────────┘
```

渲染路径：`WriteableBitmap`（BGRA8, `PixelFormat.Bgra8888`）→ `Lock()` 拿后缓冲指针 → 把该指针 + pitch 传给 `ecosim_render_frame` 让 native 直接写入。缩放用 `NearestNeighbor`，> 8x 时切到"逐格矩形绘制"的矢量路径以免出现锯齿假象。（我的决定；Avalonia 12 的具体 API 名在 M0 用一个 spike 确认，见 §14）

参数面板：从 `ecosim_param_count()` / `ecosim_param_desc(i)` 生成 slider 列表。**加一个 C++ 参数 → UI 自动出现它的滑杆，C# 不改一行**，这是 anti-goal"两边同步"的正面解法。

快捷键：`Space` 播放/暂停、`S` 单步、`+/-` 速度、`R` 随机、`C` 清空、`G` 切换网格线、`1..3` 换物种（生态模式）、`Ctrl+S/O` 存/读、`Ctrl+G` 切拓扑、`Ctrl+K` 钉住快照（见附录 A.3）。模式切换走下拉菜单，不占快捷键。

## 12. 文件与目录

```
cell-ecosim/
  native/
    include/ecosim_abi.h          # 唯一边界契约
    src/core/    grid.h/.cpp, species.h, params.h/.cpp, neighborhood.h/.cpp
    src/rules/   pipeline.cpp, ecosystem.cpp, birthsurv.cpp
    src/abi/     ecosim_abi.cpp    # 导出函数，薄壳
    src/util/    error.cpp, thread_pool.cpp, rng.h (xoshiro256**，自带可复现种子)
    tools/ecosim-cli/   main.cpp   # headless: run/bench/convert/rle
    tests/               单测（自带极简断言宏，不引外部框架）+ CMake/CTest 注册
    CMakeLists.txt
  app/
    Ecosim.sln
    Ecosim.Core/          Interop, NativeWorld, World, SimClock, PatternLibrary, Timeline
    Ecosim.App/           Program.cs, App.axaml, Views/, ViewModels/, Assets/
    Ecosim.Tests/         xUnit：ABI 往返、布局断言、RLE 解析、生态守恒性
  data/patterns/*.rle
  docs/superpowers/specs/, docs/architecture.md, docs/building.md
  scripts/ build.sh, run.sh, test.sh, configure.sh
  build/ (native), bin/ obj/ (dotnet)   ← .gitignore
  .gitignore  README.md  CLAUDE.md
```

命名：C++ `snake_case` 文件与函数、`PascalCase` 类型；导出一律 `ecosim_` 前缀。C# 标准 .NET 命名。

## 13. 分期里程碑（每期都能跑起来看）

- **M0 骨架（半天）**：目录、`.gitignore`、CMakeLists、`ecosim_abi.h` 只有 create/destroy/step/version、`Ecosim.Core` 的 `NativeWorld`、一个 Avalonia 空窗口显示"步进次数在涨"、`build.sh`/`run.sh`、两条冒烟测试（native 断言 + C# P/Invoke 真调）。**关键 spike**：确认 Avalonia 12.1 的 `WriteableBitmap.Lock()` 与像素格式 API、确认 MSBuild 能把 `ecosim.dll` 拷到输出目录（`<NativeLibrary>` 或 post-build copy）。
- **M1 Life 内核（完整可用的生命游戏）**：SoA 网格 + 环形/有界拓扑 + 邻域 popcount 内核 + B/S 规则表 + 单测；`ecosim-cli bench` 报 generations/s；C# 画布渲染、播放/暂停/单步/变速、鼠标画点、图案库与 RLE 导入导出。出口条件：能跑 Gosper 枪 5000 代且 ≥ 60 FPS @1024²。
- **M2 多物种生态**：能量/年龄/resource、六阶段管线、物种表、`paint` 支持物种、`params` 描述表 + UI 自动生成参数面板、统计与折线。出口条件：三物种在随机汤下出现 ≥ 2 次可复现的捕食-猎物振荡（固定种子 + 断言）。
- **M3 可视化与分析**：叠加层（能量/resource/物种）、直方图、事件日志（灭绝/激增检测，在 native 侧产生事件、托管侧展示）、快照与时间轴 scrubber/回放。
- **M4 打磨与文档**：缩放/平移手感与矢量降级、快捷键、`cew` 存档格式、错误与日志体验、性能调优（步进预算 8 ms、并行度自动选择）、README/架构文档、清理技术债。

## 14. 风险与预研

| 风险 | 处理 |
|---|---|
| MinGW 产的 DLL 被 .NET 加载失败（缺 libwinpthread 等运行时） | M0 即静态链接 `-static -static-libgcc -static-libstdc++`，冒烟测试跑通才算过关；否则退化为 `std::thread` 单线程或改用 CMake `MSYS Makefiles` 环境验证 |
| Avalonia 12.1 的位图 API 与本设计的 §11.2 假设不符 | M0 spike 验证 `Lock()`/像素格式；若不能直写后缓冲，退化为"native 写 `FrameBuffer`，UI `CopyTo`"（多一次 memcpy，1024² 量级仍可接受） |
| 6 阶段管线里 feed/reproduce/disperse 互相争用同一 `nxt` 格子 → 结果依赖线程顺序 | 管线内部约定：每阶段只写自己负责的字段，跨阶段冲突用"先在 sense 阶段生成候选表、再按固定顺序 apply"解决；单测断言"单线程 vs 多线程结果逐位一致"（固定种子） |
| 生态参数调不平（全灭绝或爆炸） | 内置 3 组已验证的预设参数（`stable` / `oscillating` / `boom-bust`）进 native 参数表；测试按预设断言代际存活率 |
| 双缓冲 + 快照环形缓冲内存膨胀 | 快照存 RLE 压缩的 `species` 平面（不含 energy），上限 64 帧，超出覆盖最旧 |
| NuGet 镜像（nuget.azure.cn）拉包失败 | 已确认可达；若离线，Avalonia 包提前 `dotnet restore` 到本地缓存并写入 building.md |

## 15. 留给实现者的自由

分带行高的具体取值与 halo 处理方式、popcount/邻域查表的写法、内部头文件切分、`EcosimStats` 的字段顺序、xUnit 用例的枚举粒度、Avalonia 控件命名与样式细节、CLI 的子命令拼写、`cew` 容器内部的压缩选择 —— 按本文约束自行决定，不改动架构和分期出口条件。ABI 的演进遵守附录 A.1 的"**只增不改**"规则：既有 struct 的字段顺序与既有函数签名不可变更，新增导出、新增尾部字段属正常演进（由 `struct_size` 与 `ECOSIM_ABI_VERSION` 兜底）。

## 16. 我决定、但你还没点头的事（可随时推翻）

1. C# 目标框架 `net8.0`（机器上同时有 .NET 10 SDK；选 8 是为了和 Avalonia 支持矩阵最稳的交集）。
2. 物种模型如上：藻 / 虫 / 兽 三物种 + Life 退化模式；名字和默认参数都可以改。
3. MVVM 手写、不引 CommunityToolkit.Mvvm。
4. 快照/回放为环形缓冲 64 帧（不做完整历史录像）。
5. native 单测用自带断言宏 + CTest，不引 Catch2/GoogleTest（少一个外部依赖，也省构建时间）。
6. 存档格式：Life 图案用标准 RLE（和 Golly 等工具互通），世界快照用自定义 `cew` 二进制容器。
7. 不做 GPU 后端、不做插件系统（列在 Non-goals）。

## 17. 附录 A：builder check 决议

设计文档写完后备一个未参与讨论的工程师审阅，提出了 6 个"猜错会改变方案"的问题。用户当时离线且已授权直接实施，因此全部由我裁定如下，**每一条都是我的决定，用户可以在醒来后推翻**。正文相应小节已按这些决议修正；本附录保留问题原样以便追溯。

### A.1 ABI 承诺不足（模式/拓扑热切换、事件流、单格查询、热力叠加通道）

审阅指出 §10 被 §15 写成"不可改动的契约"，但 UI 承诺里有四处没有跨边界手段。

决议：

1. **契约是"只增不改"，不是"冻结"**。§15 的措辞改为：ABI 的既有字段顺序、既有函数签名不可改；**新增导出与新 struct 字段属于正常演进**（`struct_size` + `ECOSIM_ABI_VERSION` 就是为此存在的）。
2. 新增/延后按下表落地，不在本期硬造：

| 缺口 | 决议 | 落点 |
|---|---|---|
| 拓扑切换（`Ctrl+G`） | **M1 热改**：加 `int32_t ecosim_set_topology(EcosimWorld*, int32_t)`（只换演化时如何读边界，状态无损） | M1 |
| 模式切换（Life ↔ 生态） | **不做热切**。切模式 = 托管侧用同尺寸 + 同 `species` 平面重建 world；文档明确"切模式会清零能量/年龄"（本来语义就不同，热改是幻觉） | M2 |
| 单格详情（悬停） | **不需要新导出**：`grid_view` 的借用指针已经给出四个平面，托管侧在锁内直读。已覆盖 | M1 |
| 热力叠加（能量 / resource） | `EcosimPalette` 尾部**追加** `int32_t layer`（0=物种 1=能量 2=resource），native 依 `struct_size` 判断字段是否存在，旧客户端不传即 layer 0 | M3 |
| 灭绝/激增事件 | 加 `int32_t ecosim_event_poll(EcosimWorld*, EcosimEvent* out, int32_t cap, int32_t* out_count)`（native 内部事件队列，托管侧每 tick 拉空）。这替代 §10 里"回调留空"的方案：**拉，不推**，避免 native 线程调回托管 | M3 |

### A.2 多线程调用谁串行化

决议：**`Ecosim.Core.NativeWorld` 内部一把 `Mutex`（`_gate`）串行化全部 native 调用**，包括 `step` / `paint` / `render_frame` / `grid_view` 读取 / `stats`。

- 因此 §11.1 的"借用指针"与 §11.2 的"零拷贝直写 `WriteableBitmap` 后缓冲"**并不冲突**：写入发生在持有 `_gate` 期间，native 不可能同时 swap 缓冲。这是正解，不是退化路径。
- §14 风险表里"退化为 `FrameBuffer` + `CopyTo`"只作为兜底（若某版 Avalonia 拿不到可写指针），不改变并发模型。
- 唯一的例外要求：`SimClock` 一次 tick 内的 `step` + 发布帧必须在**同一次锁持有**或两次顺序持有之间完成，不允许其他线程插入到 step 与 render 中间造成"渲染了 nxt 但读的是 cur"。实现约定：**`step` 返回前 native 不交换借用视图的生命周期语义**，即 `grid_view`/`render_frame` 永远看到最近一次 step 完成后的状态。

### A.3 快照回放的归属（native 环形缓冲 vs C# Timeline）

决议：**native 拥有唯一的真实快照存储；C# `Timeline` 只是视图与索引，不存第二份状态。**

- 存储：压缩全状态快照（`species` + `energy` + `resource`，run-length），**指数抽取**：最近 16 帧逐代，其后每 2 代 1 帧、每 4 代 1 帧……上限 64 帧，覆盖最旧。这样 2048² 长程回放在内存上可控（约 ≤ 60 MB 量级，而非 64×16 MB）。
- `restore` 的能量语义：快照里带 `energy`，直接恢复；**只有从纯 `species` 恢复的路径**（RLE 导入、旧格式）时，活格能量取该物种的 `initial_energy` 参数（不是 0，避免"读档即全灭"）。
- 捕获时机：自动（按上面的抽取调度）+ 手动（`Ctrl+K` 钉住当前帧，不被覆盖）。

### A.4 网格尺寸生命周期

决议：

1. 尺寸在 world 创建时确定，**没有 resize 导出**（resize 语义 = 重建 + 迁移，收益低成本高）。
2. 启动默认 `1024×1024`；侧栏"新建世界"提供宽高输入与 512/1024/2048/4096 预设，上限 `ECOSIM_MAX_DIM = 8192`，超限给中文错误提示。
3. 导入时尺寸不匹配的处理：**托管侧读档 → 按 `max(存档尺寸, 内容包围盒)` 重建 world → 载入**，并在状态栏提示"已按存档尺寸重建 2048×2048"。`from_rle` 在现有 world 上的语义是"在 (0,0) 放置图案内容"（不改尺寸、超界部分裁剪并计入 `last_error` 警告），这与读档是两个不同操作，UI 文案要分开。
4. CLI 用 `--w/--h` 构造任意尺寸（bench 依赖这条）。

### A.5 可复现性契约

决议（这条最影响正确性，定死）：

1. 种子位置：`EcosimConfig.rng_seed` 为世界级种子；`EcosimSeedConfig.seed` 为 `randomize` 的局部覆盖（`0` 表示沿用 config 种子）。
2. **RNG 无状态**：采用 counter-based 派生 `value = splitmix64(seed ^ hash(generation, band_index, cell_index))`。因此
   - 并行/串行结果**逐位一致**（§14 风险表要求的那条测试得以成立），因为每个格子的随机数不依赖调用顺序；
   - `cew` 存档**不需要序列化 RNG 内部状态**，只存种子与 `generation`。
3. `cew` 内容范围（明确）：magic + version、`EcosimConfig` 全字段、`EcosimParams` 全字段、`generation`、四个平面（RLE 压缩）。不含事件队列、不含快照环。
4. "读档后继续跑逐位接续"**不作为目标**（要它就得存状态，与第 2 点的无状态设计冲突，收益只是"复现同一条轨迹"，而轨迹可用种子 + 从头跑到该代复现）。文档与 CLI 用 `--seed` 复现即可。

### A.6 多物种图案的编码

决议：**`.rle` 严格保持 Golly 兼容的单状态语义（`b`=活、`o`=死），只用于 Life 图案与单物种退化导出；多物种一律 `.cew`。**

- `data/patterns/` 分两类：`*.rle`（block/blinker/glider/lwss/acorn/pulsar/r_pentomino/gosper_gun）与 `*.cew`（藻毯、虫群、三物种起手式）。`PatternLibrary` 同时枚举两类，UI 图标区分。
- 从生态模式导出 `.rle` 是**有损操作**（`species > 0` → `b`），需要 UI 勾选确认并写明"将丢失物种/能量信息"。
- 与 §16.6 的分类不再冲突：RLE 归 Life，`cew` 归生态，图案库两者都有。

### A.7 顺带修正的一处自相矛盾

§11.2 的快捷键 `1..4 换物种` 与 §8.2 把 Life 列为物种 id 4 相矛盾。修正为：**Life 是模式（`EcosimMode`），不是物种**；生态模式下活格与 Life 模式下活格都是 `species = 1`。快捷键 `1..3` 选物种，模式切换走下拉菜单。§8.2 表中 Life 行相应改为"退化模式，使用 species=1"。

---

## 18. 附录 B：本设计的实施拆分

| 计划文件 | 覆盖 |
|---|---|
| `docs/superpowers/plans/2026-11-21-cell-ecosim-m0-m1.md` | M0 骨架 + M1 Life 内核（本计划交付后即可完整玩生命游戏） |
| 待写 `...-m2-ecosystem.md` | §8.4 六阶段生态管线、参数描述表与自动 UI、模式切换 |
| 待写 `...-m3-visualization.md` | 热力叠加、事件流、快照与时间轴回放 |
| 待写 `...-m4-polish.md` | `cew`、性能预算、快捷键与文档 |
