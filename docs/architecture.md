# 架构说明

## 分层

```
native/  C++20 (MinGW g++)
  ecosim_core (static)   grid / kernel / kernel_fast / parallel / rle / world
  ecosim.dll (shared)    src/abi/ecosim_abi.cpp —— 唯一的 extern "C" 壳
  ecosim_tests           直接链接 ecosim_core + 同一份 ABI 壳（静态）
  ecosim-cli             同上，headless 消费者
app/
  Ecosim.Core            唯一跨边界程序集：P/Invoke、SafeHandle、时钟、图案库、Viewport
  Ecosim.App             Avalonia MVVM，只见托管门面
  Ecosim.Tests           通过真 DLL 断言（不 mock 边界）
```

为什么 native 测试不走 DLL：在这台工具链上，可执行文件链接**刚生成的导入库**会让 ld
无任何诊断地失败（两行代码的探针也复现）。因此测试与 CLI 静态链接同一份引擎对象 +
同一份 ABI 壳；跨进程/跨边界的真实加载由托管测试覆盖（它们加载的就是 `ecosim.dll`）。

## 边界契约（`native/include/ecosim_abi.h`）

- 纯 C：不透明句柄、固定宽度整数、`struct` 首字段 `int32_t struct_size`、`pack(4)`。
- **只增不改**：既有字段顺序与既有函数签名永不变更；新增导出和尾部字段是正常演进，
  由 `struct_size` 与 `ECOSIM_ABI_VERSION` 兜底（设计附录 A.1）。
- 布局一致由两侧同时钉住：`src/abi/ecosim_abi.cpp` 的 `static_assert`
  与 `app/Ecosim.Tests/AbiLayoutTests.cs`。改一边必须改另一边。
- 返回码 `0 / 负数`，细节在 `ecosim_last_error()`。**每次成功调用都会清空它**，
  所以诊断必须在失败后立刻读取（有测试盯着这条）。
- 借用指针：`grid_view` 返回的平面指针只在"下一次变更调用之前、且持锁期间"有效。

## 并发模型（设计附录 A.2）

- native 从不自己起步进线程。`SimClock`（C# 后台 Task）是唯一的 `step` 调用者。
- `NativeWorld` 内部一把 `_gate` 锁串行化全部 native 调用，包括渲染直写位图后缓冲。
  因此"借用指针"和"零拷贝 blit"才安全：step 与 render 不可能同时发生。
- `Pool`（native 行带线程池）只并行**一次 step 内部**的带；带之间写不相交的行区间，
  读共享的 halo。`run_bands` 返回即所有带完成，调用者随后交换缓冲。
- `Pool` 析构必须在锁内改停止状态再 notify，否则条件变量丢唤醒、join 永久挂起
  （这个坑踩过，见提交 5d6dbf7）。

## 确定性（设计附录 A.5）

随机数是**无状态派生**：`splitmix64(seed ^ f(generation) ^ g(index))`。
因此线程数、带序都不改变结果——`randomize_and_step_are_thread_count_independent`
钉住这条。`cew` 存档将来不需要存 RNG 内部状态。

## 内存布局

SoA 四平面：`species(u8) / resource(u8) / energy(u16) / age(u16)`，双缓冲 ping-pong。
热循环只扫 `species`；快速内核用三行滑窗的 0/1 标志行 + 256 项 popcount 表，
热循环里没有取模（torus 的 wrap 在行首尾单独处理）。

## 渲染

`ecosim_render_frame` 把每格写成调用方缓冲里的一个 BGRA32 像素（调色盘按 species 取色）。
Avalonia 侧位图是**一格一像素**，缩放平移靠 `RenderTransform` 的矩阵，
所以一帧的代价 = 一次原生渲染 + 零次托管拷贝。

### 显示节奏与模拟节奏必须解耦

时钟 tick 驱动渲染曾经导致"幻灯片感"：`Task.Delay`/`DispatcherTimer` 落在 Windows 默认
15.6ms 定时器格子上，33ms 的请求实际得到 31 或 47ms，实测帧间隔 45ms 抖动（≈22fps）、
且 `round(elapsed*target)` 每 tick 丢掉小数代，速率进一步掉到 ~22 代/秒。现在三件事一起保证：

1. **渲染泵**（`DispatcherTimer` @8ms，`DispatcherPriority.Render`）只在状态版本号变化时重绘，
   显示节奏与模拟节奏无关；
2. **`SimClock.BudgetFor`** 用累加器保留小数代，长期速率精确（纯函数，有单元测试）；
3. **`timeBeginPeriod(1)`** 让 16ms 的泵真的每 16ms 触发。

实测：帧间隔 16ms 为主（偶发 32ms），~55fps，60 代/秒设定下实际 ~58 代/秒。
单帧工作量 0.25–0.5ms（其中 `Stats()` 全表扫描 0.2ms，M2 起改增量统计）。

**改过位图像素必须显式失效 Image 视觉。** Avalonia 12 的合成渲染器不会因为
`WriteableBitmap.Lock()` 写入内容就把那个视觉标脏：它会继续用已上传的旧纹理。
症状是"画面静止，除非拖动窗口/平移才跳出新内容"。因此 `MainViewModel.Redraw()` 末尾
触发 `FramePainted`，视图订阅后调用 `CanvasImage.InvalidateVisual()`。
验证方式很直接：相隔 5 秒截同一块画布区域做哈希，修前完全相同，修后不同。

## 坐标映射（笔刷为什么必须落在光标上）

绘制落点与点击反算**共用同一份数学**：`Viewport.DestinationRect(gridW, gridH)` 给出网格在屏幕上
的矩形，视图把位图用 `Canvas.Left/Top + Width/Height + Stretch=Fill` 摆到这个矩形上，
`Viewport.ScreenToCell` 是它的反函数。`ViewportTests` 里有一条测试断言两者互为精确逆
（不同网格/画布/缩放下取三角格中心往返）。

曾经的坑：`Image` 用 `Stretch="None"`（Avalonia 语义 = 不缩放**且居中**）配合 `RenderTransform`，
于是"画在哪"和"点到哪"差了约 (canvas − grid)/2，笔刷整体偏左上。

**绘制越界按裁剪处理，不按错误处理**：`World.Paint` 先把每个矩形与网格求交再发给引擎，
空的那部分丢弃。快速拖拽经常一笔甩出边界，那是正常手势，不该抛异常；
引擎侧仍严格拒绝真正非法的 op（反向矩形、未知物种），这类继续报错，视图层用 `Guard()`
转成状态栏提示而不是崩溃。

## 图案与 RLE

- RLE 严格 Golly 单状态语义（`b/o/$/!`，`#` 注释），多物种将来走 `.cew`（附录 A.6）。
- `data/patterns/*.rle` 是唯一事实来源；**收进来的条件是行为测试通过**
  （静物不动、振荡器回到原状、飞船平移、玛土撒拉稳定）。从记忆里抄的
  Gosper 枪 / 脉冲星 / LWSS 没通过，已移除，等一个可校验的来源（见 m1-checklist）。
- `ecosim_from_rle_at` 支持偏移放置；超出网格裁剪并给出中文警告（不是错误）。

## 生态管线为什么不是"直接分带"

六阶段里有三个阶段会改**别的格子**（feed 改猎物、reproduce 往空格放子代、disperse 搬进空格）。
直接按行带并行会让结果依赖带划分，所以：

1. **候选阶段**并行、只读上一阶段缓冲，产出动作列表（`FeedAction`/`ReproAction`/`MoveAction`），
   每带一份，带序拼接即为全局格索引顺序；
2. **apply 阶段**单线程按该顺序结算，用 `taken`/`drained` 位图解决冲突；
   首选格子被占时会找下一个空格，而不是丢掉这次动作。

per-cell 的阶段（sense、metabolise、regrow）直接按带并行。
结果与线程数无关，由 `ecosystem_result_does_not_depend_on_configured_threads` 盯住；
实测 2048² 混合食物网因此从 214 ms/代 降到 40.9 ms/代（5.2×）。

## 静态库成员之间不能互相引用（归档循环）

`libecosim_core.a` 里 `world.cpp` 调用 `run_ecosystem`（在 `rules/ecosystem.cpp`），
所以 ecosystem 一侧**不能**再回调 world 的 Out-of-line 成员：GNU ld 对归档只做单向一遍扫描，
循环依赖会留下未定义引用——而且这套工具链的 ld 是 GUI 子系统程序，它的诊断在管道里**一个字都看不到**，
表现为"ld 退出码 1/5，无任何输出"。

规则：管线通过出参向上汇报（`run_ecosystem(world, &counts)`），`World` 负责记录；
需要被两边用的成员要么 inline 在头文件里（如 `World::record_generation`、`Pool& World::pool()`），
要么放在依赖链的下游。

## 分析面板的数据从哪来

| 面板 | 数据来源 | 频率 |
|---|---|---|
| 叠加层 | `EcosimPalette.layer` + `ecosim_render_planes`（快照渲染，不碰引擎锁） | 每帧 |
| 种群曲线 | `ecosim_history_count/get`：普查由内核/管线顺手算出，零额外扫表 | 4 Hz |
| 能量分布 | `ecosim_energy_histogram`：普查那一遍顺手填桶，边界只传 12 个整数 | 4 Hz |
| 事件日志 | `EventQueue`（native 判定，带滞回），**拉取**而非回调 | 4 Hz |
| 时间轴 | `SnapshotRing`（native 唯一存储，RLE 压缩 + 指数抽取） | 拖动时 |

面板刷新有预算测试盯着（`AnalysisCostTests`：512² 一次刷新 < 40 ms），
因为它跑在 UI 线程上——"顺手画个图"变成卡顿是这类应用最常见的自伤。

**面板不得全表扫。** 每个面板读数都必须来自引擎"本来就要做的那一遍遍历"
（普查顺手填历史样本与能量桶），或来自一个有界结构（事件队列、快照环的索引）。
边界上只走聚合值：12 个桶、600 条样本、64 个事件——不走平面。
反例是 M3 的第一版能量分布：托管侧拿带热力的快照自己数，等于每 250 ms
把三个平面搬过边界再扫一遍；M4 把它换成普查内增量后，这条开销归零。

## 存档容器 `cew`

`ecosim_save_cew` / `ecosim_load_cew` 读写版本化容器（magic `CEWS`）：

```
Header(40B, static_assert 钉住)：magic[4] version header_size w h generation body_size
                                plane_modes[3] reserved[9]
body：params + 三个平面（species/energy/resource，各自 RLE 或 raw，与快照环同一取舍）
```

- **只增不改**：`version` 只允许向后读（旧版本可读，未来版本报"版本过新"），
  `header_size` 让旧二进制跳过它不认识的新字段。
- age 平面不存：与快照环同一取舍（恢复时归 0，代数随存档回拨，回放因此仍然精确）。
- 错误消息是**可区分的中文**：太短 / magic 不对 / 版本过新 / 尺寸不符（消息里带两个尺寸）/
  平面损坏。UI 直接把消息显示在通知行，所以"读档失败"必须自己说明原因。
- RLE 与 cew 走不同解码路径：把 `.cew` 喂给 RLE 导入、或反之，都报格式错误而不是崩。

## 如何扩展

- **加一个物种（M2）**：改 native 物种表 + 参数描述表；调色盘槽位已预留 16 个；
  UI 的参数面板由描述表自动生成，C# 不需要改。
- **加一个参数**：同上，进 `EcosimParams` + 描述表；`struct_size` 前缀兼容旧客户端。
- **加一条规则**：`EcosimConfig.birth_mask/survive_mask` 已是数据；UI 的 B/S 编辑器
  通过"RLE 快照 → 重建 world → 还原"生效（掩码在 create 时确定）。

## 已知偏离与取舍

| 项 | 决定 | 原因 |
|---|---|---|
| CMake generator | `MSYS Makefiles`（自动探测） | 本机无 mingw32-make / ninja，pacman 镜像不可达 |
| native 测试/CLI 链接方式 | 静态链接引擎对象 | 导入库链接在本工具链上无诊断地失败 |
| 图案库内容 | 5 个已验证图案 | 行为测试是唯一准入标准 |
| 快照/回放、热力叠加、事件流 | M3 | 见设计附录 A.1/A.3 的 ABI 演进计划 |
