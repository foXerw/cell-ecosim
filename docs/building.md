# 构建与运行

## 环境（本项目在以下配置上验证）

| 组件 | 版本 / 说明 |
|---|---|
| OS | Windows 11 (22621)，Git Bash |
| C++ | MinGW `g++` 16.1（`C:\msys64\ucrt64`），**没有 MSVC、没有 clang** |
| 构建 | CMake 4.4 + `make`（`C:\msys64\usr\bin\make.exe`） |
| .NET | SDK 8.0.303 与 10.0.203；应用目标框架 `net8.0` |
| Avalonia | 12.1.6（NuGet；本机 NuGet 源被重定向到 nuget.azure.cn 镜像） |

`scripts/configure.sh` 会按机器上实际存在的构建工具依次尝试 `ninja` → `MinGW Makefiles` →
`MSYS Makefiles` → `Unix Makefiles`。本机没有 `mingw32-make` 也没有 ninja（MSYS2 的 pacman
镜像不可达），所以走 `MSYS Makefiles`。别的机器上通常会自动选到更好的那个。

需要手工指定时：

```bash
ECOSIM_GENERATOR="Ninja" scripts/configure.sh
ECOSIM_NATIVE_DIR="D:/somewhere/build/native" scripts/run.sh   # 运行时换一份引擎 DLL
```

## 常用命令

```bash
scripts/build.sh          # 原生引擎 + 托管解决方案，一条龙
scripts/build-native.sh   # 只编 C++，并跑 ctest
scripts/build-managed.sh  # 只编 C#（需要已存在的 build/native/ecosim.dll）
scripts/test.sh           # 两套测试都跑（ctest + dotnet test）
scripts/run.sh            # 启动桌面应用
scripts/bench.sh          # headless 跑分（M1 task 7 起可用）
```

## 产物的位置

```
build/native/ecosim.dll          原生引擎（静态链接 libstdc++/libgcc/libwinpthread）
build/native/ecosim_tests.exe    native 测试，与 DLL 同目录，直接能找到它
app/*/bin/Debug/net8.0/          托管输出；ecosim.dll 由 NativeEngine.props 复制进来
```

`app/NativeEngine.props` 是托管侧找到引擎的唯一途径：它把 `build/native/ecosim.dll`
以 `Link=ecosim.dll` 复制到每个项目的输出目录。若该文件不存在（还没编过 C++），
`NativeLibraryLoader` 会再从输出目录、然后逐级向上找 `build/native/ecosim.dll`。

## 引擎 DLL 是否自带运行时

验证过：

```bash
ldd build/native/ecosim.dll | grep -E "winpthread|libstdc|libgcc"   # 应为空
```

有输出就说明 `-static -static-libgcc -static-libstdc++` 没生效，
那样从资源管理器双击启动的 C# 程序会加载失败（只有 MSYS2 shell 里能跑）。

## 性能基线

`scripts/bench.sh` 的输出会记进这里（M1 task 7 落地后填写实测数字）：

| 场景 | 网格 | ms/代 | 代/秒 |
|---|---|---|---|
| Life 随机汤 300‰（M1 时） | 1024×1024 | 0.391 | 2559 |
| Life 随机汤 300‰（M3 后，含普查与抽样快照） | 1024×1024 | 0.619 | 1615 |
| Life 随机汤 300‰（M1 时） | 2048×2048 | 1.806 | 554 |
| Life 随机汤 300‰（M3 后） | 2048×2048 | 2.649 | 378 |
| 生态·单物种汤 | 512×512 | 2.11 | 474 |
| 生态·混合食物网 | 512×512 | 3.74 | 268 |
| 生态·混合食物网 | 1024×1024 | 11.38 | 88 |
| 生态·混合食物网 | 2048×2048 | 44.63 | 22 |

（2026-11-21，MinGW g++ 16.1 -O3，`scripts/bench.sh` + 混合网探针。
设计目标：Life 1024² ≥60 FPS、生态 2048² ≥20 FPS —— 均达标，余量分别约 96× 与 1.1×。
生态 2048² 的余量很薄，任何新的每代开销都要重新量。）
