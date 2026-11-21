# M4 出口检查（2026-11-21）

## 功能门槛

| 项 | 判据 | 结果 |
|---|---|---|
| `cew` 存读 | 存→读：哈希、代数、参数逐一一致 | ✅ `test_cew.cpp` + `CewTests` |
| 截断与损坏 | 截断字节流抛 `EcosimException` 且消息含"损坏" | ✅ 两侧各一条 |
| 尺寸不符 | 消息里带**两个**尺寸（存档的与当前世界的） | ✅ `test_cew.cpp:129` |
| 格式互斥 | `.cew` 喂 RLE 导入、RLE 喂 `load_cew` 都报格式错误，不崩 | ✅ |
| RLE 解析有界 | 声明的 run 长度不再被无条件信任（死空间 O(1)、上限钳制） | ✅ `RleCodecTests` 回归 |
| UI 存/读档 | 工具栏两个按钮，默认名 `ecosim.cew` | ✅ 启动目视 |
| 能量直方图 | 普查那一遍顺手填桶；边界只传 12 个整数；桶和 == 存活数 | ✅ `HistogramTests` + native |
| 滑块停靠 | live 时 `ScrubValue == 0`、回放时 == Index；`MaximumIndex` 增长会通知 | ✅ `TimelineTests` 三条新测试 |
| 回放中刷新 | 每帧 `Refresh` 不把用户踢回 live，也不挪滑块 | ✅ `Refreshing_while_reviewing_keeps_the_reviewed_frame` |
| Ctrl+K 钉帧 | 钉住当前帧进环 + 通知行反馈；钉住的帧可被时间轴取回 | ✅ `Capture_pins_a_frame_anytime`（模型层） |
| 占位文本 | 规则输入框 `Watermark` → `PlaceholderText` | ✅ `MainWindow.axaml:159-160` |
| 并发安全 | `ReleaseHandle` 持 `_gate`；守卫测试在去掉锁后**必红** | ✅ 见下 |
| 启动不崩 | 首帧直方图为 null 不再 NRE（绑定绕过 CLR setter） | ✅ 启动 + 截图 |
| 测试规模 | 原生 102 例 / 托管 143 例全绿 | ✅ |

## 性能（门槛：Life 1024² ≥60 FPS，生态 2048² ≥20 代/秒）

| 场景 | 网格 | ms/代 | 代/秒 | 门槛 |
|---|---|---|---|---|
| Life 随机汤 300‰ | 1024×1024 | 0.683 | 1465 | ✅ |
| Life 随机汤 300‰ | 2048×2048 | 2.970 | 337 | ✅ |
| 生态·混合食物网 | 256×256 | 1.136 | 880 | ✅ |
| 生态·混合食物网 | 512×512 | 3.070 | 326 | ✅ |
| 生态·混合食物网 | 1024×1024 | 10.081 | 99 | ✅ |
| 生态·混合食物网 | 2048×2048 | 39.927 | 25.0 | ✅（余量 1.25×，仍是最薄的一档） |

M4 没有引入新的每代开销：直方图改引擎内增量后，生态四档与 M3 持平或更好。
Life 两档比 M3 记录略高（0.62→0.68、2.65→2.97），同一次连跑两遍差 7%，
判定为本机噪声（IDE 在后台）而非回归；数字与复测说明都记在 `docs/building.md`。

## 路上抓到并修掉的 bug

1. **首帧 NullReferenceException 直接带走整个窗口。** `EnergyHistogramChart.Values`
   声明成非空 `int[]` 并在 CLR setter 里 `?? Array.Empty<int>()`，但绑定写属性走
   属性系统、绕过 setter，首帧把 null 写进去，`Render` 在合成器里 NRE。
   修法：类型改成 `int[]?` 并在 `Render` 判空（与 `PopulationChart` 一致），
   同时 VM 的 `Histogram` 初始化为空数组——"还没有数据"是画得出来的状态。
2. **时间轴的滑块轨道长度永远是 0。** `TimelineModel.Refresh` 从不通知
   `MaximumIndex`，而滑块的 `Maximum` 正绑在它上面，所以无论跑多久轨道都不长。
   测试先红后绿；`Refresh` 现在把每个派生读数都通知一遍。
3. **`ReleaseHandle` 的 use-after-free。** interop 签名收 `IntPtr` 而非 `SafeHandle`，
   运行时不做引用计数，`Dispose` 可以与在飞的 `ecosim_world_step` 真竞争。
   补了确定性守卫 `Disposing_waits_for_an_in_flight_native_call_...`：
   拿掉锁它 4 ms 内必红，装上锁必绿。
4. **如实记录：200 轮"边跑边 Dispose"那条测试不是这个 bug 的守卫。**
   释放后尚未被复用的内存通常行为正常，所以交错测试抓不到它——我验证过
   （临时去锁后它照样绿）。它钉的是调用方契约：结果只能是干净停止或
   `ObjectDisposedException`。守卫与压力测试各司其职，注释里写明了。
5. 我自己在测试里写过 `raised.Append("MaximumIndex")` 这种"怎么都过"的断言，
   发现后删除——它正好把 bug 2 盖住了。

## 未能目视、改由测试钉住的项

- **滑块停靠的截图**：窗口比 1200 px 的屏幕高，底栏在屏幕外，截不到。
  行为由 `TimelineTests` 三条模型测试钉住；axaml 绑定
  （`Value` OneWay → `ScrubValue`、`Maximum` → `Timeline.MaximumIndex`、
  松手 `PointerReleased` 才提交）逐行核对过。
- **Ctrl+K 与拖回钉帧**：模型层已覆盖（`Capture_pins_a_frame_anytime`、
  `Rewind_and_replay_reproduces_the_same_board`），没有做 UI 自动化。
