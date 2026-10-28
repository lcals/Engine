# Engine

Engine 是一个 C++20 项目，目前包含静态库 `engine::core`、示例命令行程序 `engine_app` 和 CTest 测试。项目使用 CMake 与 vcpkg，在 Windows、Linux 和 macOS 上构建；示例通过 vcpkg 安装并使用 `{fmt}`。目前尚无引擎功能实现。

## 规划与现状

项目目标是开发跨平台游戏引擎。已确认的规划按模块记录：

- [引擎架构总览](docs/architecture.md)：各模块规划文档入口。
- [首个游戏](docs/first-game.md)：割草射击游戏的核心玩法循环。

当前已按运行时、编辑器和资源管线登记 vcpkg 依赖，并将 ozz-animation 与 V8 固定为 Git 子模块。示例代码仍只使用 `{fmt}`；V8 提供显式的 GN 构建与 CMake 链接验证入口。规划中的 PBR 渲染、编辑器、脚本与插件系统、MCP 服务和项目出包尚未实现。

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

这些 Preset 用于安装和发现依赖，不代表尚未实现的引擎模块已经可运行。第三方本地补丁放在 `third_party/`，构建前的覆盖流程见[开发与第三方依赖](docs/development.md)。

### V8 依赖

V8 不进入默认构建。初始化子模块、设置 `VCPKG_ROOT` 后，在原生 x64 平台显式执行：

```sh
cmake --preset v8-release
cmake --build --preset v8-prepare-release
cmake --build --preset v8-release
ctest --preset v8-release
cmake --preset v8-debug
cmake --build --preset v8-debug
ctest --preset v8-debug
```

也可以使用 `cmake --workflow --preset v8-release` 或 `v8-debug` 串联上述准备、构建与测试。首次准备会联网获取 V8 `DEPS` 固定的 depot_tools、GN、Ninja、Clang 及其他构建依赖；编译需要较多磁盘、内存和时间。默认并行度为 4，可在配置时用 `-DENGINE_V8_JOBS=2` 调低。网络环境需要代理时，在运行前设置 `HTTP_PROXY` / `HTTPS_PROXY`。

Windows 使用本机 VS 2022 C++ 工具链和 Windows SDK 10.0.26100.0，SDK 需包含 Debugging Tools for Windows。准备过程不升级系统工具链；按需通过已有 SDK 安装器添加缺少的组件。

下载、源码副本、Git 配置和编译产物均位于忽略的 `build/v8/`，不会修改 `third_party/v8/` 或用户全局 Git 配置。Windows 源码副本应用仓库保存的兼容补丁，以匹配本项目 `/MDd` / `/MD`；关闭全局 allocator shim，保留宿主 CRT 分配器。构建同时运行上游嵌入样例；`engine.v8` CTest 通过 CMake 的 `engine::v8` 目标执行 JavaScript 并检查结果及 Intl 格式化。嵌入 V8 的程序需要将 `ENGINE_V8_ICU_DATA` 指向的 `icudtl.dat` 复制到程序旁，测试目标已自动部署。当前使用 `symbol_level=0` 控制产物体积，Debug 保留 V8 断言但不生成完整调试符号。

GN 的 monolith 未收集隐式 Rust 归档。`cmake/package_v8.cmake` 按 GN 实际依赖清单额外合并 Rust 与 Clang 运行库，生成 `engine_v8.lib`（Windows）或 `libengine_v8.a`；`engine::v8` 自动使用这个完整嵌入库。原 GN 库和上游样例保留，Temporal 功能未禁用。

V8 的 Windows 本机验证结果与限制见下方“本地验证记录”；其他平台仍需实机验证。

### 基础项目

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

## 本地验证记录

2026-10-28 在 Windows x64、VS 2022 上执行：

| 范围 | 实际结果 |
| --- | --- |
| 基础 `debug` / `release` workflow | 配置、构建成功，CTest 各 3/3 通过 |
| `editor-deps` | 全部安装并配置成功，16 个直接依赖无缺项，合计 40 个包 |
| 编辑器依赖 Debug / Release | 本机集成构建成功，CTest 各 4/4 通过，包含 ozz 骨架构建和 USD `.usda` / `.usdc` 写入、重新载入 |
| bgfx 工具 | shaderc 编译顶点 shader、texturec 转换并校验 KTX 及 BGRA8/BC1/BC3/BC7 DDS、geometryc 转换三角形 OBJ，均退出 0 且输出非空 |
| V8 Debug / Release | 完整准备、构建与上游样例成功，CMake 嵌入测试与基础测试各 4/4 通过；本项目目标开启 `ENGINE_WARNINGS_AS_ERRORS` |

本机集成验证使用忽略的 `CMakeUserPresets.json` 和 `build/dependency-smoke/`，不是仓库 CI 的测试。Windows USD port 的插件资源位于对应配置的 `bin/usd/*/resources/`；本机测试按 Debug / Release 设置 `PXR_PLUGINPATH_NAME`。`texturec --validate` 对测试用 RGBA8 legacy DDS 报 `m_hasAlpha` 元数据不一致，但 4×4 样本的 16 个像素经 DDS → PNG 往返后 RGBA 值全部一致；该校验兼容问题尚未修补，其他上述格式通过。本轮未在 Linux / macOS 执行依赖编译或 V8 验证。

V8 的 Debug 隔离队列补丁另用本机回归程序验证了全局分支的启用、清空、禁用、重新启用、重复配置与销毁，保留 `/MDd` 和 `_ITERATOR_DEBUG_LEVEL=2`，退出 0；线程局部分支未单独验证。

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
| `ENGINE_ENABLE_V8` | `OFF` | 启用显式准备的 V8 monolith、构建目标与嵌入测试 |

作为 `add_subdirectory()` 子项目使用时，示例和本项目测试默认关闭；链接库目标 `engine::core` 即可。Preset 用于独立开发，会显式开启示例与测试。

`references/` 用于本地放置参考文件，内容不纳入 Git。部分实现代码可参考该目录，但文档中不得记录其中的具体内容。

详细说明见 [开发与第三方依赖](docs/development.md)，协作约定见 [AGENTS.md](AGENTS.md)。项目采用 [MIT License](LICENSE)。
