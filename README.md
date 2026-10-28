# Engine

Engine 是一个 C++20 游戏引擎项目，使用 CMake 与 vcpkg。当前已实现 `Engine` 单例、支持子系统的 `System` 生命周期、SDL3 窗口和 bgfx 最小 GPU 渲染。`engine_app` 可以显示窗口、持续清屏和调试文本，并处理缩放、暂停、恢复及退出。

## 规划与现状

项目目标是开发跨平台游戏引擎。已确认的规划按模块记录：

- [引擎架构总览](docs/architecture.md)：各模块规划文档入口。
- [首个游戏](docs/first-game.md)：割草射击游戏的核心玩法循环。

架构以[类图索引](docs/class-diagrams/index.md)为评审入口。每个系统在 `include/engine/<system>/` 和 `src/engine/<system>/` 各有独立子目录与 CMake 目标；自有头文件直接按文件名包含，搜索路径由目标传递。`Engine` 持有项目、窗口、资源、世界和渲染根系统；世界持有物理、导航、动画、运行时 UI 及可选脚本子系统。

各系统已接入相应依赖的头文件和实际库调用，Debug / Release 链接测试会实例化所有启用的系统。编辑器和资源管线是独立宿主模块，运行时不依赖它们。当前多数业务系统仅具备生命周期和依赖接入：尚未实现 PBR、场景加载、物理模拟、UI 绘制、编辑器界面、脚本执行框架或 Android APK 打包。V8 保留独立 JavaScript / Intl 测试，但 `ScriptSystem` 暂只验证版本 API。

仓库自带最小占位项目 `projects/minimal/`，后续启动统一传入这个项目：`settings/` 保存项目设置，`cache/` 保存生成缓存，`assets/` 保存项目资源；三个目录目前仅有 `.gitkeep`，缓存产物不提交。

`--project` 必须传入项目根目录。引擎在创建窗口前检查根目录及 `settings/`、`cache/`、`assets/` 三个子目录，缺失或类型不是目录就返回启动失败；本阶段不读取项目文件内容。toml++ 使用无异常表结构 API；其预编译解析接口与无异常模式存在 ABI 差异，文件解析尚未启用。Taskflow 仅接入任务图，执行器头含未隔离的异常语法，执行功能留待无异常适配后启用。

## 构建与测试

需要支持 C++20 的编译器：Windows 安装 Visual Studio 2022+ 的 C++ 桌面开发工作负载，Linux 使用 GCC 或 Clang，macOS 安装 Xcode Command Line Tools。环境脚本负责检查、安装 CMake 3.25+、Git、Ninja 和编译缓存工具；Windows 默认使用 sccache，Linux / macOS 默认使用 ccache。默认只检查，显式传入安装参数才补齐工具，安装后重新运行版本检查，失败返回非零状态。

Windows 从 Visual Studio 的 Developer PowerShell 执行以下命令（Ninja 需要已加载 MSVC / SDK 环境）：

```powershell
& .\scripts\setup.ps1 -Install
& .\scripts\setup.ps1
```

Linux / macOS：

```sh
bash scripts/setup.sh --install
bash scripts/setup.sh --check
```

macOS 使用已有 Homebrew，Linux 支持 apt-get、dnf 和 pacman，Windows 使用 winget；缺少包管理器或仓库提供的版本过旧时，脚本给出提示，不会报告安装成功。首次安装工具与构建依赖需要网络。工具运行在构建宿主机上，Android / iOS 交叉编译也使用宿主工具，不把缓存程序装进设备。

可通过 Unix 的 `--cache sccache` 或 Windows 的 `-Cache ccache` 选择另一种缓存工具；两者不需要同时安装。安装脚本参考 Game 的先检查、显式安装、安装后复查流程，后续日常构建只使用已有环境。

将 vcpkg 克隆到任意位置，运行对应系统的 `bootstrap-vcpkg.bat`（Windows）或 `bootstrap-vcpkg.sh`（Linux/macOS），再将 `VCPKG_ROOT` 设置为 vcpkg 的绝对路径。PowerShell 示例：

```powershell
git clone https://github.com/microsoft/vcpkg.git ..\vcpkg
& ..\vcpkg\bootstrap-vcpkg.bat
$env:VCPKG_ROOT = (Resolve-Path ..\vcpkg).Path
git submodule update --init
cmake --workflow --preset ninja-debug
```

Linux / macOS 的 shell 示例：

```sh
git clone https://github.com/microsoft/vcpkg.git ../vcpkg
../vcpkg/bootstrap-vcpkg.sh
export VCPKG_ROOT="$(cd ../vcpkg && pwd)"
git submodule update --init
cmake --workflow --preset ninja-debug
```

vcpkg 依赖版本由仓库中的 `vcpkg.json` 固定，Git 子模块由仓库提交固定；已有 vcpkg 安装时，直接设置 `VCPKG_ROOT` 即可。首次配置会自动安装所选依赖。

推荐使用 `ninja-debug` / `ninja-release` Preset，构建完整的最小窗口运行时（不含 V8），三端统一使用 Ninja 并要求可运行的编译缓存工具。原 `debug` / `release` 保留平台默认生成器。按需启用编辑器、资源管线或 V8；`editor-deps` 同时启用运行时与资源管线。

```sh
git submodule update --init
cmake --preset runtime-deps
cmake --preset asset-pipeline-deps
cmake --preset editor-deps
```

这些配置会建立对应系统目标。编辑器 Debug / Release workflow 编译全部非 V8 系统，运行生命周期、依赖链接、命令行，以及 TBB / USD 回归测试：

```sh
cmake --workflow --preset editor-deps
cmake --workflow --preset editor-deps-release
```

要把所有已选依赖（包括 V8）编译、链接并验证，使用：

```sh
cmake --workflow --preset ninja-engine-debug
cmake --workflow --preset ninja-engine-release
```

这两个 workflow 包括显式 V8 准备步骤；已准备过且依赖未变时，可用 `cmake --preset ninja-engine-debug`、`cmake --build --preset ninja-engine-debug`、`ctest --preset ninja-engine-debug` 分步复跑。原 `engine-debug` / `engine-release` 提供相同系统组合，使用平台默认生成器。脚本系统不启动 isolate，因此最小窗口程序当前无需加载 ICU 数据；真正执行 JavaScript 的测试会自动部署 `icudtl.dat`。

依赖安装缓存位于 `build/dependencies/`，按 `runtime`、`editor`、`asset-pipeline`、`base` 四组 feature 隔离；同组 Debug / Release 共用安装目录，避免重复安装。vcpkg 自带二进制缓存继续生效，并保留用户设置的 `VCPKG_BINARY_SOURCES`。删除某个应用构建目录不会清掉这些共享依赖；只修改引擎源码时，无需运行清理依赖或重新准备依赖的命令。

CMake 通过 `CXX_COMPILER_LAUNCHER` 等目标属性接入缓存，覆盖引擎、自有测试与 ozz；Windows 优先查找 sccache，其他宿主优先查找 ccache，也可用 `ENGINE_COMPILER_CACHE_PROGRAM` 指定可执行文件。已有 `CMAKE_CXX_COMPILER_LAUNCHER` 设置优先保留。Windows 缓存目标使用 `/Z7` 将调试信息放入对象文件，避免共享编译 PDB 阻止命中。用 `ccache --show-stats` 或 `sccache --show-stats` 查看实际命中。

该机制适用于 Makefiles / Ninja，Visual Studio 生成器不支持此启动器属性；新的 `ninja-*` 预设要求缓存工具存在且可执行，缺失时提示运行环境安装脚本。需要显式关闭时同时传入 `-DENGINE_REQUIRE_COMPILER_CACHE=OFF -DENGINE_USE_COMPILER_CACHE=OFF`。配置缓存 `CMakeCache.txt` 不等于对象文件缓存，Debug / Release 的不同编译选项仍分别缓存。对应机制见 [CMake 启动器文档](https://cmake.org/cmake/help/latest/prop_tgt/LANG_COMPILER_LAUNCHER.html)和 [sccache 的 CMake / MSVC 说明](https://github.com/mozilla/sccache#usage)。

ozz 的源码副本仅在固定提交、覆盖文件或准备脚本变化，以及副本缺失时更新；内容相同的文件保留时间戳，无变化构建不重复运行准备。V8 使用独立 GN 工具链及成功构建缓存，不经过 CMake 的编译启动器：校验固定源码、补丁、平台 / SDK / 工具链和 GN 参数后，Debug / Release 分别复用已验证的库和头文件；命中时跳过同步、GN、Ninja 与归档合并。缺失产物或输入变化会使缓存失效，失败的构建不会留下成功标记。V8 自带的固定版本 Ninja 保持独立，不替换为环境安装的版本。

第三方本地补丁放在 `third_party/`，构建前的覆盖流程见[开发与第三方依赖](docs/development.md)。

### V8 依赖

V8 不进入默认构建。初始化子模块、设置 `VCPKG_ROOT` 后，在原生 x64 或 macOS ARM64 平台显式执行：

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

macOS 使用本机 Xcode SDK 与链接器，Clang、Rust 仍由固定 V8 `DEPS` 提供。与 Windows 相同，嵌入库保留宿主的 `malloc/new`，关闭全局 allocator shim。为兼容系统 SDK，GN 关闭显式 Clang 模块并使用 Xcode 链接器；当下载的 Clang 包缺少 `libLTO.dylib` 时，构建副本中的兼容补丁通过 `xcrun` 选择现有 Xcode 库，并让 Rust 编译期宏使用同一链接包装器。另为 SDK 27 的 C++20 数学头补充缺失的 `INFINITY` / `NAN` 宏，并在编译器不支持 `stack_protector_ignore` 时，仅在 V8 的 signpost 调用处忽略该属性诊断。

下载、源码副本、Git 配置和编译产物均位于忽略的 `build/v8/`，不会修改 `third_party/v8/` 或用户全局 Git 配置。Windows 源码副本应用仓库保存的兼容补丁，以匹配本项目 `/MDd` / `/MD`；关闭全局 allocator shim，保留宿主 CRT 分配器。构建同时运行上游嵌入样例；`engine.v8` CTest 通过 CMake 的 `engine::v8` 目标执行 JavaScript 并检查结果及 Intl 格式化。嵌入 V8 的程序需要将 `ENGINE_V8_ICU_DATA` 指向的 `icudtl.dat` 复制到程序旁，测试目标已自动部署。当前使用 `symbol_level=0` 控制产物体积，Debug 保留 V8 断言但不生成完整调试符号。

GN 的 monolith 未收集隐式 Rust 归档。`cmake/package_v8.cmake` 按 GN 实际依赖清单额外合并 Rust 与 Clang 运行库，生成 `engine_v8.lib`（Windows）或 `libengine_v8.a`；macOS 先从 Clang 通用运行库提取本机架构的副本，再合并归档。`engine::v8` 自动使用这个完整嵌入库，并在 macOS 传递 Foundation、CoreFoundation 和 Security 框架依赖。原 GN 库和上游样例保留，Temporal 功能未禁用。

V8 各平台的实际验证范围与限制见下方“本地验证记录”。

### 最小窗口

```sh
cmake --workflow --preset ninja-debug
cmake --workflow --preset ninja-release
```

也可以逐步执行：

```sh
cmake --preset ninja-debug
cmake --build --preset ninja-debug
ctest --preset ninja-debug
```

Ninja 预设写入独立的 `build/ninja-debug/`、`build/ninja-release/`，避免与旧生成器的构建目录冲突。原 `debug` / `release` 预设采用平台默认生成器，保留在 `build/debug/`、`build/release/`；Windows 通常为 Visual Studio 工程，Linux / macOS 通常为 Makefiles。

程序目标名为 `engine_app`。Linux / macOS 的 Ninja 预设下运行：

```sh
./build/ninja-debug/engine_app --project projects/minimal
# 渲染 120 帧后自动退出，适合冒烟验证。
./build/ninja-debug/engine_app --project projects/minimal --frames 120
```

Windows 的 Ninja 预设对应 `.\build\ninja-debug\engine_app.exe --project projects/minimal`；旧 Visual Studio 预设对应 `.\build\debug\Debug\engine_app.exe --project projects/minimal`。macOS / iOS 使用 Metal，Windows 使用 DirectX 12，Android / Linux 使用 Vulkan；所选后端不可用时返回失败，不以 Noop 或其他后端回退代替成功。

普通 CTest 不要求桌面环境。要在有图形桌面的机器运行真实窗口/GPU测试：

```sh
cmake --preset ninja-debug -DENGINE_ENABLE_GPU_TESTS=ON
cmake --build --preset ninja-debug
ctest --preset ninja-debug
```

`engine.window_render` 验证真实后端、缩放后的像素尺寸、后台暂停恢复、关闭、重复初始化和 Engine 调度。该测试有 60 秒超时并串行执行；不需要 GPU 的生命周期和依赖测试始终注册。

## CI

CI 仅通过 `workflow_dispatch` 手动触发。在 GitHub 仓库的 Actions 页面选择 `ci`，点击 Run workflow 并选择分支；提交、推送、创建或更新 PR、合并分支均不会自动运行 CI。助手只有在用户明确要求运行 CI 时才可手动触发。

## 本地验证记录

下面保留依赖准备阶段的历史结果；其中“基础示例”和 Noop 测试不代表本轮窗口实现。新系统的验证结果另列在本节末尾。

2026-10-28 在 Windows x64、VS 2022 上执行：

| 范围 | 实际结果 |
| --- | --- |
| 基础 `debug` / `release` workflow | 配置、构建成功，CTest 各 3/3 通过 |
| `editor-deps` | 全部安装并配置成功，16 个直接依赖无缺项，合计 40 个包 |
| 编辑器依赖 Debug / Release | 本机集成构建成功，CTest 各 4/4 通过，包含 ozz 骨架构建和 USD `.usda` / `.usdc` 写入、重新载入 |
| bgfx 工具 | shaderc 编译顶点 shader、texturec 转换并校验 KTX 及 BGRA8/BC1/BC3/BC7 DDS、geometryc 转换三角形 OBJ，均退出 0 且输出非空 |
| V8 Debug / Release | 完整准备、构建与上游样例成功，CMake 嵌入测试与基础测试各 4/4 通过；本项目目标开启 `ENGINE_WARNINGS_AS_ERRORS` |

本机集成验证使用忽略的 `CMakeUserPresets.json` 和 `build/dependency-smoke/`，不是仓库 CI 的测试。Windows USD port 的插件资源位于对应配置的 `bin/usd/*/resources/`；本机测试按 Debug / Release 设置 `PXR_PLUGINPATH_NAME`。`texturec --validate` 对测试用 RGBA8 legacy DDS 报 `m_hasAlpha` 元数据不一致，但 4×4 样本的 16 个像素经 DDS → PNG 往返后 RGBA 值全部一致；该校验兼容问题尚未修补，其他上述格式通过。该次 Windows 验证未覆盖 Linux / macOS。

V8 的 Debug 隔离队列补丁另用本机回归程序验证了全局分支的启用、清空、禁用、重新启用、重复配置与销毁，保留 `/MDd` 和 `_ITERATOR_DEBUG_LEVEL=2`，退出 0；线程局部分支未单独验证。

2026-10-28 在 macOS ARM64、AppleClang 21 上执行：

| 范围 | 实际结果 |
| --- | --- |
| 基础 `debug` / `release` workflow | 配置、构建成功，CTest 各 3/3 通过 |
| `editor-deps` | 标准 Preset 安装和配置成功，16 个直接依赖无缺项，合计 39 个包，包含依赖的 Debug / Release 构建，无需 `--allow-unsupported` |
| 编辑器依赖 Debug / Release workflow | 开启 `ENGINE_WARNINGS_AS_ERRORS`，配置、构建成功，CTest 各 5/5 通过，包含 `engine.tbb` 与 `engine.usd` 回归 |
| 全依赖本机集成 Debug / Release | 编译、链接成功，CTest 各 6/6 通过，额外覆盖全部直接依赖的基本调用和 ozz 骨架构建 |
| bgfx 工具 | shaderc 以 `--platform osx --profile 120` 编译顶点 shader；texturec 转换并校验 RGBA8 KTX、BGRA8/BC1/BC3/BC7 DDS；geometryc 转换三角形 OBJ，均退出 0 |
| Basis Universal 工具 | ETC1S `.basis`、UASTC `.ktx2` 编码与 `-validate` 均退出 0，并完成多种 GPU 格式转码 |
| V8 Debug / Release | ARM64 完整准备、GN 构建、56 个 Rust 归档合并与上游样例运行成功，CMake 嵌入测试与基础测试各 4/4 通过；本项目目标开启 `ENGINE_WARNINGS_AS_ERRORS`，Debug 保留断言 |

本机修复保存在 `third_party/overlay-ports/`，从 manifest 固定 baseline 复制 port，保持依赖版本不变，不直接修改上游检出或安装目录：

- `basisu`：上游 `supports: !android & !arm` 会排除 ARM64。本地声明额外允许已验证的 `arm64-osx`，保留 Android 和其他 ARM 平台限制；升级 baseline 并确认上游 port 已支持 macOS ARM64 后移除此覆盖。
- `tbb`：macOS 使用共享库，让多个 USD 动态库共享同一个调度器；同时回补 oneTBB [PR #1839](https://github.com/uxlfoundation/oneTBB/pull/1839)（提交 `132268ddd04d74e8b2e21e431a7e770761def49f`），按具体任务类型的大小释放内存，保留原类布局。升级 baseline 已包含该修复后可移除补丁；共享库设置须保留到上游 port 提供等效处理，或 USD 的链接方式已不再重复嵌入静态 TBB。

本次在独立检出中同步并验证了上述修复；日志、输入资源、输出产物与本地验证脚本位于忽略的 `build/local-deps/`。上述五种纹理各经 8×8 PNG 往返后逐像素比较，RGBA 通道最大误差均为 0；BC1/BC3/BC7 使用按 4×4 对齐的纯色块样本。额外对带透明度的 PNG 输出执行 `texturec --validate` 时出现图像头 `m_hasAlpha` 不一致：固定 bimg 的 KTX 读取及容器转换未同步该标志，PNG 重读按实际像素设为有透明度，导致严格头比较失败。该 RGBA8、线性、单 mip 样本经独立解析，64 个像素的全部 RGBA 值一致；此标志维护与校验问题尚未修补。Linux 未执行依赖实机验证，本轮补丁未在 Windows 重跑。

集成程序验证了各直接依赖的链接及基本调用、bgfx Noop 初始化与帧提交、efsw 通用后端的文件事件、ozz 两关节骨架构建，以及 USD 文件属性的保存与重新读取；没有验证真实窗口或 GPU 渲染。macOS USD 插件资源位于对应配置的 `lib/usd/*/resources/`，测试按 Debug / Release 设置 `PXR_PLUGINPATH_NAME`。本机集成入口保存在独立检出的 `CMakeUserPresets.json`，可从仓库根目录复跑：

```sh
cd build/local-deps/source
export VCPKG_ROOT="$(cd ../vcpkg && pwd)"
cmake --workflow --preset local-deps-debug
cmake --workflow --preset local-deps-release
```

初次 Debug 验证曾在 `.usdc` 创建期间等待 TBB 任务超时；改用共享 TBB 后，文件读写完成，但程序退出仍因 `m_private_counter >= 0` 断言中止。回补上述任务释放修复并重建 TBB / USD 后，两种配置均正常退出。正式测试覆盖超过小对象池阈值的任务执行、取消与未提交句柄销毁，以及 `.usda` / `.usdc` 格式头、属性持久化和正常退出；Debug 保留断言并使用默认并行调度。两项测试随后按无异常及 Allman 格式约定调整，文件系统错误通过 `std::error_code` 检查；重新构建后，Debug / Release 回归各 2/2 通过。

macOS V8 的兼容补丁保存在 `third_party/overrides/v8/`：`macos-system-lto-library.patch` 和 `macos-system-math.patch` 应用于当前固定的 Chromium build 提交 `81330a6e45719ab8c0d22e5548afbedb5482b068`；`macos-signpost-attribute.patch` 应用于固定 V8 源码。需要重新同步依赖时，准备流程先撤回已知补丁、同步后重新应用；缓存命中时保留补丁和文件时间戳。升级固定工具链后，分别确认普通链接及 Rust 编译期宏能选择有效 LTO 库、SDK 数学宏正常、signpost 属性获编译器支持，再移除对应补丁；GN 的模块与链接器兼容设置须另按实际 Xcode SDK 复验。

### 本轮最小引擎实现

2026-10-28 在 macOS ARM64、AppleClang 21 上，从本仓库根目录使用继承标准 Presets 的忽略本机 Presets 验证；复用此前已安装的相同版本依赖，重新编译自有系统，启用禁异常、`ENGINE_WARNINGS_AS_ERRORS` 和 `ENGINE_ENABLE_GPU_TESTS`。

- 最小运行时 Debug / Release 构建成功，CTest 各 8/8 通过；包含项目三个目录逐项缺失、文件替代、恢复后重试的校验。
- GPU 测试实际使用 Metal，覆盖窗口初始化、连续帧、像素尺寸变化、后台暂停与恢复、关闭、重复初始化与反序释放。
- Release 程序执行 `--project projects/minimal --frames 120`，实际渲染 120 帧并退出 0；另一次运行 1200 帧正常退出。窗口截图保存于忽略的 `build/minimal-window.png`。
- 资源工具测试实际编译 GLSL 顶点 shader、RGBA8 KTX 纹理和三角形 mesh，并检查产物格式与纹理尺寸。
- 全部系统（含 V8、编辑器、资源管线）的 Makefiles 与新 Ninja 预设均完成 Debug / Release 构建，CTest 各 13/13 通过，包含新增 V8 缓存失效回归。工具实测为 Ninja 1.13.2、ccache 4.14.2；环境脚本的检查与已有工具下的安装入口均通过，未重复安装。
- Ninja 的 Debug / Release 各连续运行两次 V8 准备和两次完整增量构建，均命中缓存，每次约 0.7 秒；未重新编译或链接，程序、ozz / V8 库和生成头的时间戳不变。删除单个引擎对象文件后重建，ccache 新增一次命中，恢复文件与原文件 SHA-256 相同。记录位于忽略的 `build/cache-verification.json`。
- ozz 在隔离 Git 工程中验证了 Ninja / Makefiles 的无改动构建、覆盖增删、提交与引用变化、缺失副本恢复；编译启动器验证了缺工具拒绝、用户启动器保留和交叉编译选择宿主工具。MSVC 的 `/Z7` 仅做生成命令检查，Windows / Linux 安装与缓存未做本轮实机测试。
- 安装脚本的 Unix 隔离模拟测试 11 项通过，验证只补缺失工具、包管理器映射和失败退出；运行命令为 `python3 tests/setup_test.py`，仅需 Python 标准库。Windows 模拟用例入口为 `& .\tests\setup_windows_test.ps1`，本机没有 PowerShell，未执行；它不调用真实安装器。两组模拟测试均独立于引擎 CTest。

本轮窗口实现尚未在 Windows / Linux / Android / iOS 实机验证；Android APK 构建和移动平台生命周期仍需后续验证。

## 目录

所有受版本管理的目录均使用小写；`CMakeLists.txt` 等标准文件名保持原样。

```text
.
├── .github/workflows/  # Windows、Linux、macOS 的构建与测试 CI
├── cmake/              # 目标级编译设置
├── docs/               # 架构规划与开发说明
├── include/engine/     # 引擎及各系统的公开头文件
├── projects/minimal/   # settings、cache、assets 三个占位子目录
├── references/         # 本地参考文件，内容不纳入 Git
├── scripts/            # 跨平台宿主构建工具检查与安装
├── src/engine/         # 按系统分目录的实现；src/main.cpp 为应用入口
├── tests/              # C++ 与命令行测试
└── third_party/        # vcpkg 包查找及 Git 子模块
```

## 构建选项

| 选项 | 顶层默认值 | 用途 |
| --- | --- | --- |
| `ENGINE_BUILD_APP` | `ON` | 构建最小窗口程序（同时启用运行时） |
| `ENGINE_BUILD_TESTS` | `ON` | 构建本项目测试，同时受 `BUILD_TESTING` 控制 |
| `ENGINE_USE_COMPILER_CACHE` | `ON` | 自动使用已安装的 ccache / sccache，支持 Makefiles / Ninja |
| `ENGINE_REQUIRE_COMPILER_CACHE` | `OFF` | 缓存不可用时配置失败；ninja-* Preset 开启 |
| `ENGINE_WARNINGS_AS_ERRORS` | `OFF` | 本项目目标将编译警告视为错误，CI 开启 |
| `ENGINE_ENABLE_RUNTIME_DEPS` | `OFF` | 编译运行时系统与依赖；debug/release Preset 开启 |
| `ENGINE_ENABLE_EDITOR_DEPS` | `OFF` | 编译编辑器系统与专用依赖 |
| `ENGINE_ENABLE_ASSET_PIPELINE_DEPS` | `OFF` | 编译资源管线系统，查找并验证离线工具 |
| `ENGINE_ENABLE_GPU_TESTS` | `OFF` | 注册需要图形桌面和真实 GPU 的窗口测试 |
| `ENGINE_ENABLE_V8` | `OFF` | 启用显式准备的 V8 monolith、构建目标与嵌入测试 |

作为 `add_subdirectory()` 子项目使用时，应用与测试默认关闭。只使用系统基类时链接 `engine::core`；使用完整引擎入口时启用运行时并链接 `engine::runtime`。各系统也提供单独目标，例如 `engine::project`、`engine::render`，由目标传递直接包含头文件所需的搜索路径。Preset 用于独立开发，会显式开启对应系统与测试。

`references/` 用于本地放置参考文件，内容不纳入 Git。部分实现代码可参考该目录，但文档中不得记录其中的具体内容。

详细说明见 [开发与第三方依赖](docs/development.md)，协作约定见 [AGENTS.md](AGENTS.md)。项目采用 [MIT License](LICENSE)。
