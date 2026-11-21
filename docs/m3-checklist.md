# M3 出口检查（2026-11-21）

## 功能门槛

| 项 | 判据 | 结果 |
|---|---|---|
| 叠加层 | 物种 / 能量 / resource / 年龄四层；空格在热力层是背景；能量单调；年龄饱和；resource 按萌发阈值归一 | ✅ `test_render.cpp` 5 条 |
| 旧客户端兼容 | `struct_size = 68`（无 layer 字段）仍按物种层渲染 | ✅ `palette_without_a_layer_field_still_renders_species_colours` |
| 历史环 | 每代一条样本、顺序、回绕保最新、与 `stats` 逐值一致、与线程数无关 | ✅ `test_history.cpp` + `HistoryTests` |
| 种群曲线 | 三条物种曲线 + 代号区间 + 峰值；4 Hz 刷新而非每帧 | ✅ 截图 `m3-panel.png` |
| 事件 | 灭绝只报一次；激增带滞回（>5% 触发、<4% 重新武装）；再现；溢出丢最旧；poll 清空 | ✅ `test_events.cpp` + `EventTests` |
| 快照 | 64 槽、RLE（不划算就退回 raw）、restore 无损（age 归 0）、restore 会**回拨代数** | ✅ `test_snapshots.cpp` |
| 回放等价 | 回退到第 10 代再跑 15 代 == 从未离开的棋盘 | ✅ 原生与托管各一条 |
| 能量直方图 | 桶和 == 存活数；空板全 0；桶数钳制；thin 快照不报错 | ✅ `HistogramTests` |
| 面板开销 | 512² 一次刷新（含热力捕获 + 直方图 + 600 样本）< 40 ms | ✅ `AnalysisCostTests` |
| 测试规模 | 原生 78 例 / 托管 129 例全绿 | ✅ |

## 路上抓到并修掉的 bug

1. **归档内部循环**：管线回调 `World::record_generation` 让 `libecosim_core.a` 两个成员互相引用，
   ld 单遍扫描解不开；而这台机器的 ld 是 GUI 子系统程序，**任何诊断都进不了管道**，
   表现是"链接失败但一个字的错误都没有"。规则已写进 `docs/architecture.md` 与 `CLAUDE.md`。
2. **P/Invoke 数组默认只 `[In]`**：事件写进了临时副本，托管侧读回全零结构，
   而 `kind = 0` 恰好是 `Extinction` —— 静默假警报。改 `[In, Out]` 并让测试断言 species/count 而不仅是 kind。
3. **`capture()` 把 mode 的引用传进正在 `insert` 扩容的同一个 vector**：悬空引用。改为局部变量后写头。
4. **生态分支里多写了一个 `continue`**：跳过 `generation_ += 1`，导致每代 RNG 取值重复、
   三物种系统崩掉。M2 的振荡测试当场逮住——这条测试的存在意义被验证了一次。

## 已知问题（诚实清单）

1. **回放滑块在"实时"状态下的停靠位置可疑**：标签正确显示"实时"（`Timeline.Index == -1`），
   但截图里滑块停在约 85% 处。功能路径有测试兜着（`TimelineTests` 6 条），
   视觉绑定值得在 M4 复查：怀疑是 `Maximum` 变化时 Slider 回填了 Value。
2. **一次托管测试运行出现过"测试主机进程崩溃"**，之后无法复现（重跑 129 例全绿）。
   最可疑的是 2048² 那两条大网格测试与内存压力，但没有证据就不下结论；若再现再查。
3. `TextBox.Watermark` 在 Avalonia 12 已废弃（构建有 2 条 AVLN5001 警告），M4 换 `PlaceholderText`。
4. 面板刷新每 250 ms 做一次带热力的全平面捕获（512² 约 4 MB 拷贝）。在预算内，
   但如果之后要上"每帧直方图"，需要改成引擎侧增量统计（M4 的性能项）。

## 截图

`docs/images/m3-panel.png`：曲线（藻的大波峰、虫滞后）、能量分布柱状、事件日志、时间轴。
`docs/images/m3-energy-layer.png`：能量热力层（深色条带是被啃过/低能量的区域）。
