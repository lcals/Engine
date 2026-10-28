# Engine

Engine 是一个 C++20 项目，目前包含静态库 `engine::core`、示例命令行程序 `engine_app` 和 CTest 测试。项目使用 CMake 与 vcpkg，在 Windows、Linux 和 macOS 上构建；示例通过 vcpkg 安装并使用 `{fmt}`。目前尚无引擎功能实现。

## 规划与现状

项目目标是开发跨平台游戏引擎。已确认的规划按模块记录：

- [引擎架构总览](docs/architecture.md)：各模块规划文档入口。
- [首个游戏](docs/first-game.md)：割草射击游戏的核心玩法循环。

当前已按运行时、编辑器和资源管线登记 vcpkg 依赖，并将 ozz-animation 与 V8 固定为 Git 子模块。示例代码仍只使用 `{fmt}`；V8 的 GN 构建及各引擎模块尚未接入。规划中的 PBR 渲染、编辑器、插件系统、MCP 服务和项目出包尚未实现。

## 构建与测试

需要 CMake 3.25+、Git、vcpkg 和支持 C++20 的编译器：Windows 安装 Visual Studio 2022+ 的 C++ 桌面开发工作负载，Linux 使用 GCC 或 Clang，macOS 安装 Xcode Command Line Tools。首次构建需要网络获取依赖。

将 vcpkg 克隆到任意位置，运行对应系统的 `bootstrap-vcpkg.bat`（Windows）或 `bootstrap-vcpkg.sh`（Linux/macOS），再将 `VCPKG_ROOT` 设置为 vcpkg 的绝对路径。PowerShell 示例：

```powershell
git clone https://github.com/microsoft/vcpkg.git ..\vcpkg
& ..\vcpkg\bootstrap-vcpkg.bat
$env:VCPKG_ROOT = (Resolve-Path ..\vcpkg).Path
cmake --workflow --preset debug
```

Linux / macOS 的 shell 示例：

```sh
git clone https://github.com/microsoft/vcpkg.git ../vcpkg
../vcpkg/bootstrap-vcpkg.sh
export VCPKG_ROOT="$(cd ../vcpkg && pwd)"
cmake --workflow --preset debug
```

vcpkg 依赖版本由仓库中的 `vcpkg.json` 固定，Git 子模块由仓库提交固定；已有 vcpkg 安装时，直接设置 `VCPKG_ROOT` 即可。首次配置会自动安装所选依赖。

默认 `debug` / `release` Preset 只安装当前示例使用的基础依赖。按需验证各组依赖时，先初始化 Git 子模块，再运行对应的配置 Preset；`editor-deps` 同时启用运行时与资源管线依赖。

```sh
git submodule update --init
cmake --preset runtime-deps
cmake --preset asset-pipeline-deps
cmake --preset editor-deps
```

这些 Preset 用于安装和发现依赖，不代表尚未实现的引擎模块已经可运行；V8 还需要独立的 GN 构建接入。第三方本地补丁放在 `third_party/`，构建前的覆盖流程见[开发与第三方依赖](docs/development.md)。

```sh
cmake --workflow --preset debug
cmake --workflow --preset release
```

也可以逐步执行：

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

默认采用当前平台的 CMake 生成器，无需额外安装 Ninja。Windows 通常生成 Visual Studio 工程，Linux / macOS 通常使用 Makefiles。Debug 和 Release 分别写入 `build/debug/`、`build/release/`；Visual Studio 等多配置生成器还会在其中建立对应配置的输出目录。

示例程序的目标名为 `engine_app`。Debug 构建后，在 Linux / macOS 的默认生成器下可运行 `./build/debug/engine_app Alice`，在 Windows 的 Visual Studio 生成器下可运行 `.\build\debug\Debug\engine_app.exe Alice`，输出 `Hello, Alice!`。

## 目录

所有受版本管理的目录均使用小写；`CMakeLists.txt` 等标准文件名保持原样。

```text
.
├── .github/workflows/  # Windows、Linux、macOS 的构建与测试 CI
├── cmake/              # 目标级编译设置
├── docs/               # 架构规划与开发说明
├── include/engine/     # 库的公开头文件
├── references/         # 本地参考文件，内容不纳入 Git
├── src/                # 库实现和示例入口
├── tests/              # C++ 与命令行测试
└── third_party/        # vcpkg 包查找及 Git 子模块
```

## 构建选项

| 选项 | 顶层默认值 | 用途 |
| --- | --- | --- |
| `ENGINE_BUILD_APP` | `ON` | 构建示例命令行程序 |
| `ENGINE_BUILD_TESTS` | `ON` | 构建本项目测试，同时受 `BUILD_TESTING` 控制 |
| `ENGINE_WARNINGS_AS_ERRORS` | `OFF` | 本项目目标将编译警告视为错误，CI 开启 |
| `ENGINE_ENABLE_RUNTIME_DEPS` | `OFF` | 查找运行时依赖与 ozz-animation 子项目 |
| `ENGINE_ENABLE_EDITOR_DEPS` | `OFF` | 查找编辑器专用依赖 |
| `ENGINE_ENABLE_ASSET_PIPELINE_DEPS` | `OFF` | 查找资源管线与离线工具依赖 |

作为 `add_subdirectory()` 子项目使用时，示例和本项目测试默认关闭；链接库目标 `engine::core` 即可。Preset 用于独立开发，会显式开启示例与测试。

`references/` 用于本地放置参考文件，内容不纳入 Git。部分实现代码可参考该目录，但文档中不得记录其中的具体内容。

详细说明见 [开发与第三方依赖](docs/development.md)，协作约定见 [AGENTS.md](AGENTS.md)。项目采用 [MIT License](LICENSE)。
