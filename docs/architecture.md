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

## 图案与 RLE

- RLE 严格 Golly 单状态语义（`b/o/$/!`，`#` 注释），多物种将来走 `.cew`（附录 A.6）。
- `data/patterns/*.rle` 是唯一事实来源；**收进来的条件是行为测试通过**
  （静物不动、振荡器回到原状、飞船平移、玛土撒拉稳定）。从记忆里抄的
  Gosper 枪 / 脉冲星 / LWSS 没通过，已移除，等一个可校验的来源（见 m1-checklist）。
- `ecosim_from_rle_at` 支持偏移放置；超出网格裁剪并给出中文警告（不是错误）。

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
