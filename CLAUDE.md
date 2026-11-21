# Cell Ecosim

C++ 细胞自动机引擎（原生 DLL）+ C# Avalonia 桌面前端。用户在复习这两门语言，
所以代码要**写得像范本**：命名清楚、注释解释"为什么"而不是"是什么"、避免魔法库。

## 常用命令

```bash
scripts/build.sh           # 原生 + 托管一条龙
scripts/build-native.sh    # C++（含 ctest）
scripts/build-managed.sh   # C#
scripts/test.sh            # ctest + dotnet test，两套都要绿
scripts/run.sh             # 启动应用
```

工具链事实：Windows 11、只有 MinGW g++ 16.1（**没有 MSVC/clang**）、CMake + `C:\msys64\usr\bin\make.exe`、
dotnet SDK 8/10、Avalonia 12.1.6、应用目标框架 `net8.0`。CMake generator 由 `scripts/configure.sh` 探测。

## 分层与边界（最重要的规则）

- `native/include/ecosim_abi.h` 是 C++ 与 C# 之间**唯一的契约**。
  改它之前先读设计文档 §10 与附录 A.1："只增不改"——既有字段顺序与既有函数签名不可变更，
  新增导出与尾部字段是正常演进。每个跨边界 struct 首字段必须是 `int32_t struct_size`，
  `#pragma pack(push, 4)` 对应 C# 的 `Pack = 4`。
- 布局一致由两侧同时钉住：`native/src/abi/ecosim_abi.cpp` 的 `static_assert`
  + `app/Ecosim.Tests/AbiLayoutTests.cs`。**改一边必须改另一边**，否则构建或测试直接失败。
- 边界上禁止：C++ 对象、STL、异常（native 内部可以抛，`src/abi` 必须捕获）、引用、`bool`、宽字符。
- `app/Ecosim.Core/Interop/NativeMethods.cs` 是全仓库**唯一**允许出现 `DllImport` 的文件；
  `Ecosim.App` 只准用 `World` / `SimClock` 这些托管门面。
- UI 里没有模拟逻辑，native 里没有 UI 概念、不读写文件路径。
- **native 不拥有时钟**：只有 `SimClock`（C# 后台线程）会调 `ecosim_world_step`。
  `NativeWorld` 内部那把 `_gate` 锁串行化所有 native 调用，因此"`grid_view` 借用的指针"和
  "直写 `WriteableBitmap` 后缓冲"才是安全的（附录 A.2）。任何新增的读原生内存的路径都要在同一把锁里。
- `libecosim_core.a` 的成员之间不能形成引用循环（ld 单遍扫描归档，且它的错误消息在这个
  shell 里完全看不见）。管线用出参汇报，`World` 记录；两边都要用的成员 inline 在头里。
- 随机数是无状态派生（`splitmix64(seed, generation, cell_index)`，附录 A.5.2），
  所以"线程数不影响结果"是必须保住的性质，有测试盯着它。

## 测试

- native：`native/tests/*.cpp` 用 `check.h` 的 `ECOSIM_TEST` / `CHECK_*`，故意不引外部框架。
- 托管：`app/Ecosim.Tests` 通过真 DLL 断言，不 mock 边界。
- 期望值必须是**手算**的（例如滑翔机 4 步后整体平移 (1,1)），不许"先跑一遍再把输出抄进测试"。

## 文档

- 设计与全部取舍：`docs/superpowers/specs/2026-11-21-cell-ecosim-design.md`（附录 A 是审阅问答的裁定）
- 当前计划：`docs/superpowers/plans/2026-11-21-cell-ecosim-m0-m1.md`（M2/M3/M4 到点再写）
- 构建细节与性能基线：`docs/building.md`

## 风格

- C++：`snake_case` 文件与函数，`PascalCase` 类型，内部命名空间 `ecosim::`，导出一律 `ecosim_` 前缀。
- C#：标准 .NET 命名；interop DTO 的字段保留小写下划线名，便于和 C 头文件逐行对照。
- 面向用户的文字用中文，代码注释用英文。提交信息用英文 conventional commits。
