# 构建、测试与交付

[首页](../README.md) · [开发与维护](development.md) · [验证范围与限制](testing.md)

本文命令均从仓库根目录执行。根 `CMakePresets.json` 是配置、构建与测试的共同入口；插件装配见[插件选择](plugins/plugins.md#插件选择)，脚本工具和后端准备见[脚本模块](plugins/runtime/scripting.md)。

## 宿主环境

需要支持 C++20 的编译器：Windows 安装 Visual Studio 2022+ 的 C++ 桌面开发工作负载，Linux 使用 GCC 或 Clang，macOS 安装 Xcode Command Line Tools。环境脚本负责检查、安装 CMake 3.30+、Git、Ninja 和编译缓存工具；Windows 默认使用 sccache，Linux / macOS 默认使用 ccache。Ozz Animation 0.17 起要求 CMake 3.30，因此根预设与环境检查同步提高最低版本。默认只检查，显式传入安装参数才补齐工具，安装后重新运行版本检查，失败返回非零状态。

macOS 默认及最低部署目标为 **13.3**，与新版 V8 使用的系统 libc++ 格式化接口一致。`CMAKE_OSX_DEPLOYMENT_TARGET` 可显式提高；低于 13.3 时配置明确失败。引擎目标和 V8 GN 构建使用同一值，iOS 等其他平台保留各自工具链要求。

发行到较旧 macOS 时，第三方静态库也必须按相同部署目标构建；修改主工程的值不会改变已经安装的依赖。仅在当前宿主构建、运行成功，不能据此认定完整程序已在 macOS 13.3 验收。

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

可通过 Unix 的 `--cache sccache` 或 Windows 的 `-Cache ccache` 选择另一种缓存工具；两者不需要同时安装。安装脚本先检查、按显式请求安装，再复查工具版本；日常构建使用已有环境。

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

bgfx 使用本地 overlay 固定到 bgfx.cmake `1.167.9605-606`（API 167），归档同时包含匹配版本的 bx、bimg 和宿主 Shader 工具。运行库与 `shaderc` 必须同步升级，配置时会核对工具报告的 API 和目标头文件；旧 Shader 二进制不能沿用。内置渲染及特效 Shader 随工具更新重编，项目 Shader 经正常增量导入刷新。保留的本地补丁及固定提交见 [overlay 说明](../third_party/overlay-ports/README.md)。

## 根目录 Presets

推荐使用 `ninja-debug` / `ninja-release` Preset，构建窗口运行时（不包含脚本插件），三端统一使用 Ninja 并要求可运行的编译缓存工具。`debug` / `release` 使用平台默认生成器。按需启用编辑器、资源管线或脚本插件；`editor-deps` 同时启用运行时与资源管线。

```sh
git submodule update --init
cmake --preset runtime-deps
cmake --preset asset-pipeline-deps
cmake --preset editor-deps
```

这些配置会建立对应系统目标。编辑器 Debug / Release workflow 编译编辑器及基础运行时系统；仅 Release workflow 运行三项基础检查，模块功能按[测试文档](testing.md)选择：

```sh
cmake --workflow --preset editor-deps
cmake --workflow --preset editor-deps-release
```

要构建完整引擎、已选依赖和默认 QuickJS 脚本插件，并执行基础检查，使用：

```sh
cmake --workflow --preset ninja-engine-debug
cmake --workflow --preset ninja-engine-release
```

这两个 workflow 包括统一的 `engine_script_prepare` 步骤，并启用默认 QuickJS 桌面脚本运行；Debug 到构建成功为止，Release 另执行基础 CTest。先按[脚本运行准备](plugins/runtime/scripting.md)准备 Node.js、npm、Python 3 和示例项目的 TypeScript 依赖。已准备过且依赖未变时，可用对应预设的 configure/build 分步复跑，测试使用 `ctest --preset ninja-engine-release`。`engine-debug` / `engine-release` 提供相同系统组合，使用平台默认生成器。QuickJS 构建不准备、编译或链接 V8/ICU。显式 V8 构建也默认裁剪 Intl/ICU，因此默认产物均无 `icudtl.dat`。

运行态 MCP 与临时脚本注入使用独立的 `ninja-engine-test-debug` / `ninja-engine-test-release` 配置、构建预设；它们显式设置 `ENGINE_ENABLE_TEST_SUPPORT=ON`，使用独立构建目录。正常预设默认关闭该开关。开关按目标传递到核心契约、脚本插件及调用宿主，正式包不保留临时执行声明和实现。测试产物带测试名称，不能通过正式项目导出目标发布。测试能力与是否启用 CTest、Debug / Release 优化等级相互独立；用法见[MCP 测试接入](plugins/editor/mcp.md#独立运行程序测试)。

单配置生成器使用 `CMAKE_BUILD_TYPE`；多配置生成器由 build / test preset 的 `configuration` 选择同名配置。受影响的 C++ 修改完成 Debug / Release 构建，Debug 仅要求编译成功，CTest、功能测试及实际运行验收使用 Release。Debug 不提供默认测试流程；本机已验证范围见[验证记录](testing.md)，不能由预设存在推断平台已经通过。

## 依赖与缓存

### 依赖分组

根目录 `vcpkg.json` 按用途划分可选 feature；`builtin-baseline` 和 `overrides` 固定 vcpkg 版本。默认 `debug` / `release` 构建窗口运行时及其系统依赖；`editor-deps` 增加编辑器与资源管线，`engine-debug` / `engine-release` 再增加默认 QuickJS 脚本插件。各系统通过真实 API 调用参与编译和链接，依赖集成测试实例化所有启用的系统；各领域功能的实际支持范围见所属模块文档。

| 分组 | vcpkg feature | 依赖 |
| --- | --- | --- |
| 游戏运行时 | `runtime` | SDL3、bgfx、flecs、oneTBB、Jolt Physics、Recast Navigation、RmlUi、FlatBuffers、Basis Universal |
| 桌面编辑器 | `editor` | 运行设置读取所用 toml++、Dear ImGui（docking 与 SDL3 绑定） |
| 编辑器与离线资源管线 | `asset-pipeline` | OpenUSD、SQLite、efsw、FlatBuffers、toml++、Basis Universal、meshoptimizer、bgfx 编译工具（shaderc、texturec、geometryc） |
| 编辑器建模插件 | `modeling` | Earcut、Manifold（含 Clipper2 依赖）；仅选中建模插件时加入 |
| 编辑器资源导入 | `font-import`、`usd-import`、`movie-import` | 统一 `asset_import` 插件所需的 FreeType、OpenUSD，以及宿主 FFmpeg/ffprobe（VP9 转码）；离线工具不进入运行包 |
| 脚本插件 QuickJS 后端 | `quickjs` | quickjs-ng；仅选中脚本插件且 `ENGINE_SCRIPT_BACKEND=quickjs` 时加入 |

编辑器建模与场景烘焙共用固定 Git 子模块 xatlas；场景烘焙另使用 lightmapper。只有选择对应插件时才创建这些第三方目标，运行包不链接编辑器烘焙依赖。

影片导入工具由 `movie-import` 宿主 feature 准备 FFmpeg 与 ffprobe，配置时检查两者可运行且 FFmpeg 包含 `libvpx-vp9`。编辑器导出时随工具部署许可证和所需动态库；运行时只使用 libvpx，不部署离线编码工具。开发环境也可通过 `ENGINE_FFMPEG_EXECUTABLE` 和 `ENGINE_FFPROBE_EXECUTABLE` 显式指定已准备的宿主工具；仅在未部署工具包时，开发版编辑器可通过 `ENGINE_IMPORT_TOOL_DIR` 指定工具目录。影片格式和当前限制见[影片导入](plugins/editor/class-diagrams/movie-importer.md)。

RmlUi 关闭默认 FreeType 字体引擎，使用核心 `FontSystem` 消费已导入的曲线字体资源。FreeType 仅供统一资源导入插件的 FontImporter 使用，不进入运行时依赖和 Android 包。

运行时通过固定提交的 Git 子模块引入 ozz-animation，作为 CMake 子项目接入，关闭工具、示例与上游测试。脚本依赖选择、准备入口与后端限制见[脚本后端选择](plugins/runtime/scripting.md#脚本后端选择)和[V8 依赖](plugins/runtime/scripting.md#v8-依赖)。

桌面依赖 Preset 面向 Windows、macOS、Linux；Android 使用 `arm64-android-engine` overlay triplet 与独立安装目录。`basisu` 使用 2.50 的本地 overlay，桌面保留宿主工具，Android ARM64 与 iOS 关闭工具但保留库。SDL3 在 Android 使用 PIC 静态库并安装同版 Java 入口，与所选 VM、NDK C++ 运行库及引擎一起合入唯一的 `libmain.so`。维护与验证说明见[验证范围与限制](testing.md)。OpenUSD 仅在桌面资源管线中使用，不进入游戏运行时。
Basis Universal 固定到官方稳定版 [`v2_50`](https://github.com/BinomialLLC/basis_universal/releases/tag/v2_50)（提交 `9bebe16726b3a61c8c213eeee3b7cffb462ef34e`），基于 vcpkg 2.50#1 port 更新现有 overlay；无需升级整个 vcpkg baseline。编码与转码共用同一 port，KTX2 的 Zstandard 使用 vcpkg `zstd`。编辑器图片导入器读取相邻 meta 的压缩、质量、色彩空间和 mip 参数生成 GUID 缓存，运行时按设备支持转码并创建纹理；参数与支持范围见[资源管线](editor/asset-pipeline.md)。

需要验证依赖时先运行 `git submodule update --init`，再按需使用 `runtime-deps`、`asset-pipeline-deps` 或 `editor-deps` 的 configure / build / test Preset。编辑器 Preset 同时启用运行时、编辑器和资源管线 feature；构建及 CTest 验证相应系统与真实依赖的编译、链接和基本调用。

### 共享安装和编译缓存

依赖安装缓存位于 `build/dependencies/`，按 `runtime`、`editor`、`asset-pipeline`、`base` 四组 feature 隔离；Windows 静态预设另用 `build/dependencies/windows-static/`。同组 Debug / Release 共用安装目录，避免重复安装。vcpkg 自带二进制缓存继续生效，并保留用户设置的 `VCPKG_BINARY_SOURCES`。删除某个应用构建目录不会清掉这些共享依赖；只修改引擎源码时，无需运行清理依赖或重新准备依赖的命令。

CMake 通过 `CXX_COMPILER_LAUNCHER` 等目标属性接入缓存，覆盖引擎、自有测试与 ozz；Windows 优先查找 sccache，其他宿主优先查找 ccache，也可用 `ENGINE_COMPILER_CACHE_PROGRAM` 指定可执行文件。已有 `CMAKE_CXX_COMPILER_LAUNCHER` 设置优先保留。Windows 缓存目标使用 `/Z7` 将调试信息放入对象文件，避免共享编译 PDB 阻止命中。用 `ccache --show-stats` 或 `sccache --show-stats` 查看实际命中。

该机制适用于 Makefiles / Ninja，Visual Studio 生成器不支持此启动器属性；`ninja-*` 预设要求缓存工具存在且可执行，缺失时提示运行环境安装脚本。需要显式关闭时同时传入 `-DENGINE_REQUIRE_COMPILER_CACHE=OFF -DENGINE_USE_COMPILER_CACHE=OFF`。配置缓存 `CMakeCache.txt` 不等于对象文件缓存，Debug / Release 的不同编译选项仍分别缓存。对应机制见 [CMake 启动器文档](https://cmake.org/cmake/help/latest/prop_tgt/LANG_COMPILER_LAUNCHER.html)和 [sccache 的 CMake / MSVC 说明](https://github.com/mozilla/sccache#usage)。

ozz 的源码副本仅在固定提交、覆盖文件或准备脚本变化，以及副本缺失时更新；内容相同的文件保留时间戳，无变化构建不重复运行准备。V8 使用独立 GN 工具链与成功构建缓存，具体命中条件、失败清理和独立 Ninja 约束见[V8 依赖](plugins/runtime/scripting.md#v8-依赖)。

第三方本地补丁放在 `third_party/`，构建前的覆盖流程见[上游依赖的本地补丁](development.md#上游依赖的本地补丁)。

### 平台依赖兼容

依赖维护需保留以下兼容约束：

- `basisu` 2.50 overlay 保留 Android ARM64 与 iOS 的纯库构建，以 `Threads::Threads` 表达线程依赖；普通编码状态尊重 `m_status_output`，额外诊断进入 stderr。官方 vcpkg port 仍排除 Android，提供等效支持后才能移除覆盖。
- Android SDL3 静态合入 `libmain.so` 并安装同版本 Java 入口，bgfx overlay 传递所需 Android 系统库，避免 Java / JNI 或链接依赖不一致。
- bgfx Metal overlay 在绑定 uniform 前按需分段预留空间，并检查每次写入边界；旧段等待 GPU 完成后释放，避免大型场景超过固定 8 MiB 缓冲时越界。macOS / iOS 使用该补丁；上游提供等效修复后再移除，详见[overlay 说明](../third_party/overlay-ports/README.md)。
- macOS TBB 使用共享库，防止 USD 动态库重复嵌入独立调度器；当前版本已包含原任务内存释放修复，不再维护旧补丁及其独立冒烟测试。
- USD 插件资源在 Windows 动态依赖中位于对应配置的 `bin/usd/*/resources/`，静态依赖及 macOS 位于 `lib/usd/*/resources/`。Windows 静态编辑器将注册信息和 schema 数据部署到 EXE 旁的 `usd/`，数据继续外置；实际导入使用 `editor.usd_pipeline` 验证。

## Windows 静态构建

Windows 本机兼容修复包括 UTF-8 入口和 Shader 工具、MSVC/Ninja 诊断缓存、DX12 压缩纹理小 mip 与字体 Shader 布局，以及 SDL 只读共享和原子替换。CRT 断言和 abort 输出到 stderr，失败仍返回非零状态。

Windows 静态构建使用 `windows-static-debug` / `windows-static-release` 预设，选择 `x64-windows-static` 依赖及 `/MTd` / `/MT` CRT，构建并行度为 16，脚本默认使用 QuickJS；对应的 `windows-static-debug-prepare` / `windows-static-release-prepare` 调用统一脚本准备入口。选择 V8 时使用 `windows-static-v8-debug` / `windows-static-v8-release` 及各自 `-prepare` 预设，V8 并行度同为 16。先完成环境与项目 TypeScript 依赖准备，再在仓库根目录的 Developer PowerShell 执行：

```powershell
cmake --preset windows-static-release
cmake --build --preset windows-static-release-prepare
cmake --build --preset windows-static-release
ctest --preset windows-static-release
cmake --build --preset windows-static-release --target engine_export
.\build\windows-static-release\editor_app.exe --project .\projects\showcase
.\output\windows\x64\release\runtime\engine-showcase.exe
```

Debug 使用对应的 `windows-static-debug` 和 `windows-static-debug-prepare`，编辑器仍通过 `--project` 指定源项目。上例合并目标中的 `engine_project_export` 将运行程序导出到 `output/windows/x64/release/runtime/`，其中包含 `engine-showcase.exe` 和 `assets/`；`assets/` 保存 `catalog.bin` 及其引用的 `packs/` 或 `gen/`。默认构建不部署 ICU 数据。EXE 所在目录下的 `assets/` 是 App 的资源读取根，资源保持独立文件，随整个目录一起分发。

V8 的默认功能裁剪、独立缓存身份及可选开关见[V8 依赖](plugins/runtime/scripting.md#v8-依赖)。

## 自定义工具链

如需显式选择编译器或生成器，在一个新的构建目录配置，避免复用不同工具链的 CMake 缓存，并将 vcpkg 工具链一并传入：

```sh
cmake -S . -B build/custom -G Ninja -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build/custom
ctest --test-dir build/custom -R '^engine\.(framework|guid_map|system)$' --output-on-failure --no-tests=error
```

此示例需要本机已有 Ninja 和 Clang；使用 MSVC + Ninja 时，从 Visual Studio Developer PowerShell 执行并改用 `cl`。机器专属设置可以写入未纳入版本管理的 `CMakeUserPresets.json`。

## 交付目录

编译目录 `build/` 保留增量缓存、静态库和调试符号；最终交付物放在仓库根 `output/<platform>/<arch>/<debug|release>/`，其中 `editor/` 为桌面编辑器，`runtime/` 为项目运行包。仅提交 `output/.gitkeep` 保留空目录，生成的程序和资源不提交 Git。依赖尽量静态合入程序。

macOS 与 Windows 静态预设提供 `engine_editor_export`、`engine_project_export` 和合并目标 `engine_export`。例如 macOS 在完成根 Preset 的依赖准备和构建后执行：

```sh
cmake --preset macos-native-release
cmake --build --preset macos-native-release-prepare
cmake --build --preset macos-native-release --target engine_export
```

`ENGINE_EXPORT_PROJECT`、`ENGINE_EXPORT_MANIFEST`、`ENGINE_EXPORT_PRODUCT` 分别指定项目、资源构建清单和产品名，默认使用 showcase。运行资源按清单增量构建，复制后再次检查目录及实体，再替换输出；失败保留上一版，回滚失败保留备份并报告路径。

- Windows：`output/windows/x64/release/runtime/engine-showcase.exe`；编辑器为相邻 `editor/engine-editor.exe`，使用静态依赖和 CRT。
- macOS ARM64 默认原生模式：`output/macos/arm64/release/runtime/engine-showcase`；编辑器为相邻 `editor/editor_app`，直接从终端运行。必要私有库放 `lib/`，离线导入工具放 `tools/`，不经过 `.app` 安装或交互签名流程。`macos-native-debug` / `macos-native-release` 预设显式固定此模式；其他 Mac 桌面预设也默认 `ENGINE_MACOS_APP_BUNDLE=OFF`。
- Android ARM64：`output/android/arm64/<debug|release>/runtime/engine-showcase.apk`；原生部分仅一个 `libmain.so`。APK 继续使用测试签名。

原生桌面模式的 `catalog.bin`、`packs/` 或 `gen/` 位于程序旁的 `assets/`；默认不包含 ICU 数据。可选 `.app` 模式将它们放在 `Contents/Resources/assets/`，由 SDL 直接读取；宿主优先使用已发布的外部持久资源版本，否则使用包内版本，缺失条目不混用旧包。运行时直接读取资源，不使用自解压程序。编辑器继续要求 `--project`；运行包直接进入 Gameplay。若需要 macOS 原生应用包，配置时显式加 `-DENGINE_MACOS_APP_BUNDLE=ON`，再运行相同导出目标；程序、私有工具和初始资源均进入 `.app`，外部持久目录用于更新资源版本。本地构建仅使用系统要求的 ad-hoc 代码签名，不要求证书或用户确认；正式分发的开发者签名和公证另由发布流程提供。Linux 暂未提供或验证上述导出入口；iOS 使用下文的独立平台导出入口。

编辑器导出包含 Shader 编译器、Node.js、引擎声明、Schema 和固定版本的 TypeScript。导入优先使用项目安装的 TypeScript；项目未安装时，只有声明版本与随包版本一致才使用随包编译器。部署目录存在但工具缺失会报告失败，避免意外借用构建电脑的工具。资源运行包不包含这些编辑器工具。

macOS 编辑器的 `lib/` 同时包含 USD 导入所需的 `libusd_*` / TBB，以及随包 Node.js 工具的依赖。当前 Node.js 使用 `libnode`、`libicu*`、SSL 等动态库；这些属于离线 TypeScript 编译工具，不是引擎脚本 VM 的依赖，不受 `ENGINE_SCRIPT_BACKEND` 切换影响，也不进入项目运行包。引擎默认不部署的 `icudtl.dat` 与 Node.js 的 ICU 动态库需要区分。

## 构建选项

| 选项                                | 顶层默认值 | 用途                                                                                          |
| ----------------------------------- | ---------- | --------------------------------------------------------------------------------------------- |
| `ENGINE_BUILD_APP`                  | `ON`       | 构建引擎宿主；启用编辑器时另建 editor_app；Android 输出原生共享库                             |
| `ENGINE_BUILD_TESTS`                | `ON`       | 登记测试并默认构建三项基础检查；模块测试显式指定目标，全量构建使用 `engine_tests`，同时受 `BUILD_TESTING` 控制 |
| `ENGINE_USE_COMPILER_CACHE`         | `ON`       | 自动使用已安装的 ccache / sccache，支持 Makefiles / Ninja                                     |
| `ENGINE_REQUIRE_COMPILER_CACHE`     | `OFF`      | 缓存不可用时配置失败；ninja-* Preset 开启                                                     |
| `ENGINE_WARNINGS_AS_ERRORS`         | `OFF`      | 本项目目标将编译警告视为错误，CI 开启                                                         |
| `ENGINE_ENABLE_RUNTIME_DEPS`        | `OFF`      | 编译运行时系统与依赖；debug/release Preset 开启                                               |
| `ENGINE_ENABLE_EDITOR_DEPS`         | `OFF`      | 编译编辑器系统与专用依赖                                                                      |
| `ENGINE_ENABLE_ASSET_PIPELINE_DEPS` | `OFF`      | 编译资源管线系统，查找并验证离线工具                                                          |
| `ENGINE_RUNTIME_PLUGINS` | 按运行时依赖选择 | 运行时依赖开启时包含 `resource_graph;character_controller;effects;terrain;destruction`，脚本开启时加入 `script`；分号分隔，支持显式空列表 |
| `ENGINE_EDITOR_PLUGINS` | 按已启用能力选择 | 选择满足依赖的编辑器插件，含资源管线启用时的 `terrain` 和 `destruction`；完整默认清单见[插件说明](plugins/plugins.md)，支持显式空列表 |
| `ENGINE_ENABLE_GPU_TESTS`           | `OFF`      | 注册需要图形桌面和真实 GPU 的窗口测试                                                         |
| `ENGINE_ENABLE_TRACY` | `OFF` | 编译 Tracy 按需客户端与分段 CPU/帧/bgfx 统计采样，默认关闭 |
| `ENGINE_ENABLE_V8` | `OFF` | 未装配脚本插件时启用独立 V8 验证；脚本插件由 `ENGINE_SCRIPT_BACKEND` 决定依赖 |
| `ENGINE_SCRIPT_BACKEND` | `quickjs` | 脚本插件编译期后端，只接受 `quickjs` 或 `v8` |
| `ENGINE_V8_ENABLE_WEBASSEMBLY` | `OFF` | V8 WebAssembly，全部平台默认裁剪 |
| `ENGINE_V8_ENABLE_I18N` | `OFF` | V8 Intl/ICU，默认不部署 ICU 数据 |
| `ENGINE_V8_ENABLE_TEMPORAL` | `OFF` | V8 Temporal，全部平台默认裁剪 |
| `ENGINE_V8_ENABLE_SYSTEM_INSTRUMENTATION` | `OFF` | V8 系统 instrumentation，全部平台默认裁剪 |
| `ENGINE_ENABLE_SCRIPT_RUNTIME`      | `OFF`      | 默认选择运行时 script 插件；显式插件清单优先，完整桌面 engine 及 Android 预设开启 |
| `ENGINE_ENABLE_TEST_SUPPORT`    | `OFF`      | 额外构建可显式装配 MCP 通道的独立测试宿主；正式运行宿主不包含此入口，参见 [MCP 测试接入](plugins/editor/mcp.md#独立运行程序测试) |

## 作为子项目集成

作为 `add_subdirectory()` 子项目使用时，应用与测试默认关闭。使用 Engine 与系统基类时链接 `engine::core`；使用 RuntimeHost 装配运行时时启用运行时并链接 `engine::runtime`；可选插件由宿主另行链接及初始化。各系统也提供单独目标，例如 `engine::project`、`engine::render`；目标统一传递 `include/engine/` 搜索根，调用方使用 `runtime/`、`editor/`、`plugins/runtime/` 或 `plugins/editor/` 开头的头文件路径。Preset 用于独立开发，会显式开启对应系统与测试。

## WebGPU 游戏导出

只导出浏览器运行包，编辑器与资源构建工具继续在本机运行。Web 专用入口、Canvas、存储、页面模板和打包工具位于 `platforms/web/`，目标装配位于 `cmake/platforms/web.cmake`。类与生命周期见 [Web 宿主](runtime/class-diagrams/web-host.md)。

宿主需要 Ninja、ccache、Python 3.11+、Node.js/npm 和仓库固定的 vcpkg。下例在 macOS/Linux 的仓库根目录执行；SDK 已安装时直接激活和加载环境即可，不重复下载。

```sh
git clone --depth 1 --branch 7.0.0 https://github.com/emscripten-core/emsdk.git build/toolchains/emsdk
build/toolchains/emsdk/emsdk install 7.0.0
build/toolchains/emsdk/emsdk activate 7.0.0
source build/toolchains/emsdk/emsdk_env.sh
npm --prefix platforms/web ci
cmake --preset web-host-release
cmake --build --preset web-host-release --target asset_build_app
cmake --preset web-wasm32-release -DENGINE_WEB_ASSET_BUILDER="$PWD/build/web-host-release/asset_build_app"
cmake --build --preset web-wasm32-release --target engine_export_web
python3 platforms/web/serve.py output/web/wasm32/release/runtime/engine-showcase
```

Web 依赖安装到 `build/dependencies/web/`；原生 shaderc 启用 WGSL/Tint，Wasm 使用 QuickJS、pthread、Emdawnwebgpu 和单线程 bgfx。GPU 回读等待通过 Asyncify 让出应用 worker。SDK 版本和 IDBFS 原文件校验固定，补丁仅应用到构建目录；来源见 [WebGPU 说明](../third_party/notices/webgpu.md)。Debug 使用 `web-wasm32-debug`，只要求编译成功。

`ENGINE_EXPORT_PROJECT`、`ENGINE_EXPORT_MANIFEST` 和 `ENGINE_EXPORT_PRODUCT` 指定项目、资源清单和应用身份。导出前须关闭同项目原生编辑器；编辑器 idle 或 preview 仍持有原生目标租约。共享管线负责拒绝跨目标导入，重新打开原生编辑器时按原生目标增量重建缓存。

输出为 `output/web/wasm32/<debug|release>/runtime/<product>/`，包含 `index.html`、启动模块、`engine.mjs`、`engine.wasm`、必要 worker、独立 `assets/`、`settings/runtime.json` 与许可证。运行文件必须匹配成功链接回执；整包校验后原子发布，失败保留已有输出。资源不嵌入 Wasm，不包含源工程、SQLite 或本机工具。

页面需要安全上下文、WebGPU、OffscreenCanvas 和跨源隔离；部署服务器设置 `Cross-Origin-Opener-Policy: same-origin`、`Cross-Origin-Embedder-Policy: require-corp`，为 `.wasm` 返回 `application/wasm`、`.mjs` 返回 JavaScript MIME。附带服务仅绑定本机回环地址。页面 Start 由用户触发，浏览器音频及输入权限继续遵守浏览器规则。

首版完整预载所选运行包，再复用普通运行时资源加载；没有 WebGL/Noop 回退、远程分包流式更新或 Web 编辑器。正式构建排除运行态测试端点及临时脚本执行。覆盖范围见 [WebGPU 验证](testing.md#webgpu-导出)。


## iOS 原生游戏导出

在 Apple Silicon Mac 上安装并完成 Xcode 首次初始化，准备 Ninja、ccache 和固定的 vcpkg。平台入口、沙盒实现及打包器位于 `platforms/ios/`，装配见 `cmake/platforms/ios.cmake`，生命周期见 [iOS 类图](runtime/class-diagrams/ios-host.md)。

```sh
cmake --preset ninja-engine-release
cmake --build --preset ninja-engine-release --target asset_build_app
cmake --preset ios-arm64-device-release -DENGINE_IOS_ASSET_BUILDER="$PWD/build/ninja-engine-release/asset_build_app"
cmake --build --preset ios-arm64-device-release --target engine_export_ios
cmake --preset ios-arm64-simulator-release -DENGINE_IOS_ASSET_BUILDER="$PWD/build/ninja-engine-release/asset_build_app"
cmake --build --preset ios-arm64-simulator-release --target engine_export_ios
```

`ENGINE_IOS_ASSET_BUILDER` 指向已按桌面预设编译的原生 `asset_build_app`；示例沿用已有本机工具目录，实际可使用自己的桌面构建目录，不在目标 SDK 中执行导入工具。配置前设置 `VCPKG_ROOT`；项目、产品和资源清单仍通过 `ENGINE_EXPORT_PROJECT`、`ENGINE_EXPORT_PRODUCT`、`ENGINE_EXPORT_MANIFEST` 指定。导出需关闭同一项目的原生编辑器，避免跨目标缓存互相覆盖。

Debug 对应 `ios-arm64-device-debug` / `ios-arm64-simulator-debug`，只要求编译成功。真机与模拟器均为 arm64，分别使用 iphoneos / iphonesimulator SDK、独立 triplet、构建目录和 `build/dependencies/ios-device/` / `ios-simulator/` 安装根；不能复用另一个 SDK 的库。预设最低部署目标为 iOS 17.0，允许提高但拒绝降到依赖下限以下。当前脚本后端为 QuickJS，可关闭脚本插件；选择 V8 明确失败。

产物根为 `output/ios/arm64/<debug|release>/runtime/<device|simulator>/<product>/`，内含 `<product>.app`、输入运行文件的成功链接回执 `ios-build.json`，以及签名后可执行文件 / Info.plist 校验值和导出状态 `export.json`。应用资源位于 `.app/assets/`，汇总设置位于 `.app/settings/runtime.json`，不使用 macOS Contents 布局。导出器检查 Mach-O SDK / 架构、链接回执、资源实体与元数据；全部复制和签名成功后才原子替换产品目录。

模拟器包使用 ad-hoc 签名。真机缺少签名材料时导出状态明确为 unsigned，只能用于编译和包内容检查；不会生成可安装 IPA。需要真机部署时，通过本机 CMake 参数提供 `ENGINE_IOS_SIGN_IDENTITY` 与 `ENGINE_IOS_PROVISIONING_PROFILE`，可显式设置 `ENGINE_IOS_EXPORT_IPA=ON`；证书和描述文件不进入 Git。`ENGINE_IOS_BUNDLE_IDENTIFIER` 指定应用标识，描述文件必须覆盖该标识、未过期且包含实际签名证书。

`ENGINE_IOS_ICON` 可指向编译后的 Assets.car，`ENGINE_IOS_PRIVACY_MANIFEST` 可指向应用自己的隐私 plist；导出器校验格式并随包签名，发布声明仍由应用维护，不自动推断上架合规性。

测试使用 `ios-arm64-simulator-test-release` 预设，显式选用 runtime/mcp 和 `ENGINE_ENABLE_TEST_SUPPORT`；测试应用使用 `-test` 可执行名和 `.test` bundle identifier。复用现有沙盒文件通道、场景准备状态和 PNG 截图，不增加网络端点。正式预设排除这些声明和链接目标。安装、通道文件和截图的读取使用 Xcode / iOS 平台工具，运行前须完成 Xcode 初始化及所需设备授权。当前验证边界见 [iOS 测试](testing.md#ios-原生运行包)。
