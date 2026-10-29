# Engine

Engine 是一个 C++20 游戏引擎项目，使用 CMake 与 vcpkg。当前已实现 `Engine` 单例、支持子系统的 `System` 生命周期、SDL3 窗口和 bgfx 最小 GPU 渲染。`engine_app` 可以显示窗口、持续清屏和调试文本，并处理缩放、暂停、恢复及退出。

## 规划与现状

项目目标是开发跨平台游戏引擎。已确认的规划按模块记录：

- [引擎架构总览](docs/architecture.md)：各模块规划文档入口。
- [首个游戏](docs/runtime/first-game.md)：割草射击游戏的核心玩法循环。

架构以[类图索引](docs/class-diagrams/index.md)为评审入口。`include/engine/` 与 `src/engine/` 按 `runtime/`、`editor/`、`plugins/` 对应组织；运行时各系统继续独立成目录，公共工具放在 `runtime/utils/`。自有头文件从层级名开始包含，例如 `runtime/resource/resource_system.hpp`、`editor/editor_system.hpp`、`plugins/mcp/mcp_system.hpp`，搜索根由目标传递。`Engine` 持有窗口、资源、世界和渲染根系统；世界持有物理、导航、动画、运行时 UI 及可选脚本子系统。引擎启动不需要项目，项目是编辑器宿主的启动上下文。

规划文档及独立类图也按 `docs/runtime/`、`docs/editor/`、`docs/plugins/` 分层；统一评审入口与跨层引用关系保留在 `docs/class-diagrams/`。项目、资源管线和资源同步归编辑器层，MCP 归插件层；它们可以使用运行时能力，运行时不反向依赖它们。

各系统已接入相应依赖的头文件和实际库调用，Debug / Release 链接测试会实例化所有启用的系统。项目、编辑器和资源管线是独立宿主模块，运行时不依赖它们。当前尚未实现 PBR、场景加载、物理模拟、UI 绘制或完整编辑器界面。桌面编辑器已接入 V8 ES 模块、项目玩法入口、默认写盘日志、完整资源版本同步和引擎热重载；使用方式见下方“桌面脚本与同步”。

Android 已提供 ARM64 原生构建和 APK 打包入口，平台资源集中在 `platforms/android/`，默认启用 V8，并保留已完成的 JavaScript / Intl / Temporal 嵌入验证。新增玩法脚本闭环目前仅接入桌面；Android 继续使用系统版本探针及独立 V8 测试，待用户审核后再扩展，历史 APK 结果不能作为新增脚本能力的验证。

仓库自带最小项目 `projects/minimal/`：TS 配置、package 与锁文件放 `settings/`，TS 源码和其他资源放 `assets/`，生成 JS 与资源镜像放 `cache/assets/`。示例入口为 `assets/scripts/game.ts`；缓存产物及 `node_modules/` 不提交。

`engine_app` 使用默认引擎配置即可启动，不接受 `--project`。`editor_app --project <path>` 必须传入项目根目录；编辑器宿主先通过 `ProjectSystem` 检查根目录及 `settings/`、`cache/`、`assets/` 三个子目录，缺失或类型不是目录就返回失败，不创建窗口；检查通过后才准备脚本并启动引擎及借用项目的 `EditorSystem`。`ProjectSystem` 自身仍只负责目录检查，TS 设置由资源管线读取。toml++ 使用无异常表结构 API，其文件解析尚未启用。Taskflow 仅接入任务图，执行器功能留待无异常适配后启用。

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

这两个 workflow 包括显式 V8 准备步骤，并启用桌面脚本运行；先按下节准备 Node.js、npm、Python 3 和示例项目的 TypeScript 依赖。已准备过且依赖未变时，可用 `cmake --preset ninja-engine-debug`、`cmake --build --preset ninja-engine-debug`、`ctest --preset ninja-engine-debug` 分步复跑。原 `engine-debug` / `engine-release` 提供相同系统组合，使用平台默认生成器。启用脚本运行的应用及测试会自动将 `icudtl.dat` 部署到可执行文件旁。

依赖安装缓存位于 `build/dependencies/`，按 `runtime`、`editor`、`asset-pipeline`、`base` 四组 feature 隔离；同组 Debug / Release 共用安装目录，避免重复安装。vcpkg 自带二进制缓存继续生效，并保留用户设置的 `VCPKG_BINARY_SOURCES`。删除某个应用构建目录不会清掉这些共享依赖；只修改引擎源码时，无需运行清理依赖或重新准备依赖的命令。

CMake 通过 `CXX_COMPILER_LAUNCHER` 等目标属性接入缓存，覆盖引擎、自有测试与 ozz；Windows 优先查找 sccache，其他宿主优先查找 ccache，也可用 `ENGINE_COMPILER_CACHE_PROGRAM` 指定可执行文件。已有 `CMAKE_CXX_COMPILER_LAUNCHER` 设置优先保留。Windows 缓存目标使用 `/Z7` 将调试信息放入对象文件，避免共享编译 PDB 阻止命中。用 `ccache --show-stats` 或 `sccache --show-stats` 查看实际命中。

该机制适用于 Makefiles / Ninja，Visual Studio 生成器不支持此启动器属性；新的 `ninja-*` 预设要求缓存工具存在且可执行，缺失时提示运行环境安装脚本。需要显式关闭时同时传入 `-DENGINE_REQUIRE_COMPILER_CACHE=OFF -DENGINE_USE_COMPILER_CACHE=OFF`。配置缓存 `CMakeCache.txt` 不等于对象文件缓存，Debug / Release 的不同编译选项仍分别缓存。对应机制见 [CMake 启动器文档](https://cmake.org/cmake/help/latest/prop_tgt/LANG_COMPILER_LAUNCHER.html)和 [sccache 的 CMake / MSVC 说明](https://github.com/mozilla/sccache#usage)。

ozz 的源码副本仅在固定提交、覆盖文件或准备脚本变化，以及副本缺失时更新；内容相同的文件保留时间戳，无变化构建不重复运行准备。V8 使用独立 GN 工具链及成功构建缓存，不经过 CMake 的编译启动器：校验固定源码、补丁、平台 / SDK / 工具链和 GN 参数后，Debug / Release 分别复用已验证的库和头文件；命中时跳过同步、GN、Ninja 与归档合并。缺失产物或输入变化会使缓存失效，失败的构建不会留下成功标记。V8 自带的固定版本 Ninja 保持独立，不替换为环境安装的版本。

第三方本地补丁放在 `third_party/`，构建前的覆盖流程见[开发与第三方依赖](docs/development.md)。

### V8 依赖

桌面基础预设不启用 V8，完整引擎及 Android 预设启用。初始化子模块、设置 `VCPKG_ROOT` 后，在原生 x64 或 macOS ARM64 平台显式执行：

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

下载、源码副本、Git 配置和编译产物均位于忽略的 `build/v8/`，不会修改 `third_party/v8/` 或用户全局 Git 配置。Windows 源码副本应用仓库保存的兼容补丁，以匹配本项目 `/MDd` / `/MD`；关闭全局 allocator shim，保留宿主 CRT 分配器。桌面构建同时运行上游嵌入样例；`engine.v8` CTest 通过 CMake 的 `engine::v8` 目标执行 JavaScript，检查 Intl 格式化和 Temporal 日期及时区运算。Android 交叉编译只构建上游样例，执行验证放到目标设备上的 APK 测试中，不在宿主运行 Android 程序。嵌入 V8 的程序需要将 `ENGINE_V8_ICU_DATA` 指向的 `icudtl.dat` 复制到程序旁，测试目标已自动部署。当前使用 `symbol_level=0` 控制产物体积，Debug 保留 V8 断言但不生成完整调试符号。

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
./build/ninja-debug/engine_app
# 渲染 120 帧后自动退出，适合冒烟验证。
./build/ninja-debug/engine_app --frames 120
```

Windows 的 Ninja 预设对应 `.\build\ninja-debug\engine_app.exe`；旧 Visual Studio 预设对应 `.\build\debug\Debug\engine_app.exe`。macOS / iOS 使用 Metal，Windows 使用 DirectX 12，Android / Linux 使用 Vulkan；所选后端不可用时返回失败，不以 Noop 或其他后端回退代替成功。

编辑器宿主由 `editor-deps` 或 `ninja-engine-debug` 等启用编辑器的预设提供，启动时传入项目：

```sh
./build/ninja-engine-debug/editor_app --project projects/minimal
```

该入口验证项目并建立引擎、ImGui 上下文；完整引擎预设还会构建、同步并运行项目脚本。尚未绘制完整编辑器界面。

普通 CTest 不要求桌面环境。要在有图形桌面的机器运行真实窗口/GPU测试：

```sh
cmake --preset ninja-debug -DENGINE_ENABLE_GPU_TESTS=ON
cmake --build --preset ninja-debug
ctest --preset ninja-debug
```

`engine.window_render` 验证真实后端、缩放后的像素尺寸、后台暂停恢复、关闭、重复初始化和 Engine 调度。该测试有 60 秒超时并串行执行；不需要 GPU 的生命周期和依赖测试始终注册。

### 桌面脚本与同步

桌面编辑器与 Android 共用运行时脚本和热重载实现。宿主需有可执行的 Node.js、npm 和 Python 3；Node.js 仅用于编译工具，不进入 V8 玩法环境。先安装项目锁定的 TypeScript，再构建完整引擎预设：

```sh
npm ci --prefix projects/minimal/settings
cmake --workflow --preset ninja-engine-debug
cmake --workflow --preset ninja-engine-release
```

从仓库根目录启动编辑器，指定一个持久运行目录，让编辑器和 MCP 工具访问同一份缓存与状态：

```sh
./build/ninja-engine-debug/editor_app --project projects/minimal --runtime-dir build/editor-runtime
```

Windows 将可执行文件写为 `build/ninja-engine-debug/editor_app.exe`；Release 对应 `build/ninja-engine-release/`。可加 `--frames 120` 做有限帧运行。不传 `--runtime-dir` 时使用按项目区分的系统应用数据目录，启动日志会打印实际路径。

编辑器先读取 `settings/tsconfig.json`，将 `assets/` 中的 TS 编译为 `cache/assets/` 下同路径的 JS，并复制其他资源；构建失败保留上一次完整输出。随后建立本会话不变的内置资源快照并发布持久资源版本，全部引擎系统就绪后才启动 `Game`。示例会有意写入 warning/error 日志来验证绑定，这两条消息不表示启动失败。

编辑器运行时，在另一终端修改 `projects/minimal/assets/scripts/game.ts`，然后执行：

```sh
python3 scripts/mcp_client.py --server build/ninja-engine-debug/mcp_app --project projects/minimal --runtime-dir build/editor-runtime status
python3 scripts/mcp_client.py --server build/ninja-engine-debug/mcp_app --project projects/minimal --runtime-dir build/editor-runtime sync
python3 scripts/mcp_client.py --server build/ninja-engine-debug/mcp_app --project projects/minimal --runtime-dir build/editor-runtime hot-reload on
python3 scripts/mcp_client.py --server build/ninja-engine-debug/mcp_app --project projects/minimal --runtime-dir build/editor-runtime status
python3 scripts/mcp_client.py --server build/ninja-engine-debug/mcp_app --project projects/minimal --runtime-dir build/editor-runtime hot-reload off
```

Windows 的 `--server` 使用对应 `mcp_app.exe`。客户端通过 stdio 启动 `mcp_app`，完成 MCP 初始化并调用一次工具；也可将这个可执行文件及相同参数配置到其他 MCP 客户端。`sync` 同时构建 TS 和同步完整版本；热重载开关只写控制请求，切换由引擎在帧边界执行。同步成功或返回 `pending engine decision` 不等于新脚本已经运行，应通过 `status` 的 `activeRevision`、`availableRevision`、`running` 和 `lastError` 确认。状态是引擎最近一次上报，并非独立的进程存活检测。

热重载默认关闭；同一持久目录已有控制文件时读取其设置。关闭期间仍可同步新版本，当前实例和模块快照保持不变；开启后应用最新完整版本。候选脚本编译或静态链接失败时旧实例继续运行；新模块执行或 `OnInitialize` 失败会停止此次玩法并报告错误，不承诺撤销副作用或保留任意 JS 状态。`Update` 异常记录错误，后续引擎帧继续运行。

运行 SDK 位于 [engine.d.ts](include/engine.d.ts)，由虚拟 ES 模块 `engine` 提供：

```ts
import { Debug, Gameplay } from "engine";

export default class Game extends Gameplay {
    override OnInitialize(): boolean {
        Debug.Log("Game started");
        return true;
    }

    override Update(): void {
    }

    override OnShutdown(): void {
        Debug.Log("Game stopped");
    }
}
```

每次玩法运行只有一个项目入口，可直接承载业务；它独立于节点。`OnInitialize` 必须返回布尔值。`Debug.Log`、`Debug.LogWarning`、`Debug.LogError` 接受字符串并映射到原生日志。`Node`、`Component` 和场景脚本接口尚未实现，SDK 没有声明这些接口。

`App` 的静态只读路径具有以下含义：

| 脚本接口 | 用途 |
| --- | --- |
| `App.builtinAssetsPath` | 内置资源根目录；编辑器中为当前会话的固定资源快照 |
| `App.persistentDataPath` | 持久数据根目录，保存同步版本、日志、控制与状态；可由 `--runtime-dir` 指定 |
| `App.cachePath` | 平台临时缓存目录，可重建；不承载需要跨启动保留的同步版本 |

`LogSystem` 默认将日志写入 `persistentDataPath/logs/engine.log` 并通过 SDL 输出到平台日志（桌面 stderr、Android logcat）；单文件达到 4 MiB 时轮转，保留 3 个旧文件。项目编译输出 `cache/assets/` 与平台临时 `App.cachePath` 是不同目录。

持久版本保存在 `persistentDataPath/assets/versions/<revision>/`；所有文件传入并核对大小及 FNV-1a 64 哈希后，才原子发布 `assets/available.json`。入口及每个静态 import 统一按相对路径解析：清单命中时使用对应版本文件，`deleted` 中的路径禁止回退，其他路径读取内置资源；不通过复制内置内容覆盖已有同步文件。绝对路径、越界路径和越界符号链接会被拒绝。版本文件保持不变，下一次发布使用新版本名。

文件读取统一通过 `ResourceSystem` 的 SDL3 同步接口，单文件上限 64 MiB；当前脚本源码、清单和控制文本另有 4 MiB 上限，单次模块图最多 256 个模块。运行时支持相对路径静态 ES import 和 `engine` 模块，不提供动态 import、顶层 await 或 Node.js API，也不自动打包任意 npm 运行时依赖。TS 的相对 import 应使用生成后的 `.js` 路径。

同步将完整版本发布到本机或 Android Debug 包的持久目录，通过 `script-control.json` / `script-status.json` 控制引擎；不提供独立网络服务或差量同步。`ENGINE_ENABLE_SCRIPT_RUNTIME` 在完整桌面和 Android 预设均开启；关闭时保留 V8 版本探针。Android 传输使用 adb，具体命令如下。

### Android APK

Android 平台工程位于 `platforms/android/`，沿用 [SDL3 的 Activity / JNI 框架](https://wiki.libsdl.org/SDL3/README-android)。原生部分仍由根 CMake Presets 编译，Gradle 只处理 Java、Manifest 和 APK；SDLActivity 源码从同版本 SDL3 的 vcpkg overlay 安装结果取得。APK 直接启动引擎，不包含编辑器项目目录、项目校验或编辑器 UI。

准备 Python 3、JDK 17、Android SDK Platform 35、Build Tools 36.1.0、NDK 27.2.12479018，以及上文的 CMake / Ninja / 编译缓存 / vcpkg。设置 `ANDROID_HOME`、`ANDROID_NDK_HOME`、`JAVA_HOME` 和 `VCPKG_ROOT` 指向已有安装；可复用其他工程的安装目录。Android Gradle Plugin 固定 9.0.0，wrapper 固定 Gradle 9.1.0 并核对官方 SHA-256；也可通过 `GRADLE_HOME` 复用相同版本，脚本会检查版本。

```sh
npm ci --prefix projects/minimal/settings
python3 scripts/build_android.py --config debug --project projects/minimal
python3 scripts/build_android.py --config release --project projects/minimal
```

包含 V8 的 Android 构建入口支持 macOS ARM64 / x64 和 Linux x64 宿主，Windows 宿主的 Android V8 工具链尚未接入。脚本依次调用 `android-arm64-debug` / `android-arm64-release` 的根 configure、显式 `-prepare`、build Preset，再打包。产物为 `build/apk/engine-arm64-debug.apk` 和 `build/apk/engine-arm64-release.apk`；最低 Android 9 / API 28，ABI 为 `arm64-v8a`，需要 Vulkan。两种 APK 均使用本机调试签名，供安装测试；发布签名尚未配置，签名密钥不进入仓库。

两种 APK 内的原生库均由 Gradle 去除调试符号以控制安装体积；Debug 保留断言与可调试属性。完整的 Debug 原生符号保存在 `build/android-arm64-debug/libmain.so`，可用 APK 原生库的 ELF Build ID 与之对应，无需把符号放进设备。

脚本检查 NDK 版本，并在打包前仅移除该配置的旧 APK，避免增量 ZIP 更新留下旧原生库空洞导致安装包膨胀；原生库、Java、Gradle 和依赖编译缓存继续复用。`--project` 指定打包项目，默认 `projects/minimal`，必须具有 `settings`、`assets`、`cache` 三个目录并安装 settings 中锁定的 TypeScript。根 CMake 的 bundle 目标每次重新编译 TS，并发布完整项目资源快照；原生库与上游缓存仍复用。`--skip-native` 仅重打包已生成的原生库及资源，不重新选择或编译项目。

Android 运行时包含 SDL3、bgfx、FlatBuffers、Basis Universal、flecs、Jolt、Recast / Detour、ozz、RmlUi 和 V8 的实际编译与链接。编辑器、toml++ 项目模块和离线资源管线不进入 APK。依赖共享安装在 `build/dependencies/android/`，与桌面安装目录隔离，并保留 vcpkg 二进制缓存。BasisU 的 Android overlay 只关闭宿主命令行工具，保留完整编码 / 转码库。

V8 使用独立的 `build/v8/android-arm64/` 工作副本和 Debug / Release 缓存，目标平台、NDK、API 与工具链均参与缓存身份，不复用桌面归档。宿主 Clang / Rust / GN 版本由 V8 固定，Android 使用同一 NDK 的 sysroot、compiler-rt 和 `libc++_shared.so`；关闭 ThinLTO，最终将完整嵌入依赖合并链接进 `libmain.so`。兼容补丁保存在 `third_party/overrides/v8/`，上游子模块保持只读：除 macOS 宿主和外部 NDK 配置外，NDK 27 缺失的 `std::atomic_ref` 由 V8 / simdutf 内部的 Clang 原子指令兼容分支处理，保持原有内存序与 NDK ABI，不混用其他版本的 C++ 标准库。

APK 包含 `assets/v8/icudtl.dat`、`assets/project/` 下的编译资源和 `assets/project-manifest.json` 完整清单；不包含项目设置、TS 源码或 Node.js。正常启动按 App、LogSystem、Engine 的顺序初始化，再运行项目 Gameplay；无需在手机上传入编辑器项目目录。ScriptSystem 校验 APK 清单和文件，并将清单导出到持久目录供首次同步计算删除项；ICU 根据内容复用应用临时缓存中的副本，JS 直接通过 SDL 读取 APK。

另保留可选 V8 嵌入测试，ICU 副本写入应用 `cache/v8-smoke/`，在新的 SDL 进程执行 JavaScript、SharedArrayBuffer 原子操作及十六进制 / Base64 转换、Intl，以及 Temporal 的日期和时区运算。固定 V8 版本的 Temporal 测试显式启用 `--harmony-temporal`；这不增加脚本系统的公开接口。

复用已有 ARM64 Android 模拟器，明确指定其序列号后安装和启动：

```sh
adb -s emulator-5580 install -r build/apk/engine-arm64-debug.apk
adb -s emulator-5580 shell am start -n org.engine.minimal/.EngineActivity
# 有限帧运行，完成后正常退出 Activity。
adb -s emulator-5580 shell am start -n org.engine.minimal/.EngineActivity --ei frames 120
# 在新进程中运行 APK 内的 V8 验证；EngineV8 日志应逐项报告 PASS。
adb -s emulator-5580 shell am force-stop org.engine.minimal
adb -s emulator-5580 shell am start -n org.engine.minimal/.EngineActivity --ez v8_smoke true
```

`emulator-5580` 是本轮测试实例，其他环境用 `adb devices` 确认实际序列号。已有进程要改用不同启动参数时，先用 `adb -s <serial> shell am force-stop org.engine.minimal` 停止本应用，再重新启动；不需要清除模拟器或应用数据。模拟器必须提供 Vulkan；软件 Vulkan 也可验证功能，但不能用来衡量真机 GPU 性能。

### Android 脚本同步与热重载

| App 接口 | Android 指向 |
| --- | --- |
| `builtinAssetsPath` | `apk:/assets/project`，由 ResourceSystem 读取 APK 的逻辑路径 |
| `persistentDataPath` | 应用私有 `files/`，保存日志、版本和控制状态，重启仍保留 |
| `cachePath` | 应用私有 `cache/`，可重建的 ICU 等临时数据 |

先启动 Debug APK，让引擎创建状态和内置清单。电脑上的 MCP 使用明确的设备序列号与包名，拒绝隐式选择设备：

```sh
python3 scripts/mcp_client.py --server build/ninja-engine-debug/mcp_app --project projects/minimal --android-serial emulator-5580 --android-package org.engine.minimal status
python3 scripts/mcp_client.py --server build/ninja-engine-debug/mcp_app --project projects/minimal --android-serial emulator-5580 --android-package org.engine.minimal hot-reload on
# 修改项目 assets 下的 TS 后同步；无需重新打 APK。
python3 scripts/mcp_client.py --server build/ninja-engine-debug/mcp_app --project projects/minimal --android-serial emulator-5580 --android-package org.engine.minimal sync
python3 scripts/mcp_client.py --server build/ninja-engine-debug/mcp_app --project projects/minimal --android-serial emulator-5580 --android-package org.engine.minimal status
```

adb 不在 PATH 时添加 `--adb <adb可执行文件>`。真机使用 `adb devices` 中的实际序列号；需要设备已经授权 adb 调试。MCP 运行在电脑，通过 `run-as` 写入该调试包的私有持久目录，手机不安装 MCP 服务。此通路仅支持 debuggable APK；Release 保持不可调试，同步请求会返回明确错误。

设备同步先上传暂存版本、读回验证大小与哈希，再改名为不可变版本，最后原子发布 `available.json`。发布过程使用设备目录锁；发布前失败保留旧的可用版本。最终发布回执丢失会报告未确认，此时应查询设备状态。每次同步合并 APK 导出的 `builtin.json` 与已有发布清单作为删除基线，APK 升级新增的路径也参与计算，避免删除的资源意外从 APK 恢复。脚本系统逐路径优先使用持久版本，未覆盖的路径回退 APK，删除标记禁止回退。可写版本在 `persistentDataPath`，不会因临时缓存清理而丢失。

热重载由引擎逐帧决定是否执行；关闭开关仍可同步文件，开启后加载最新版本。用 `status` 确认 `activeRevision` 与 `availableRevision` 相同且 `running=true`，同时检查 `lastError`。APK 更新或普通重启不会覆盖现有可用脚本版本；要恢复包内脚本，应先停止应用并由开发者明确清理对应同步数据。

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

- `basisu`：上游 `supports: !android & !arm` 会排除 ARM64。本地覆盖允许 macOS ARM64 与 Android；Android 关闭命令行工具，保留编码 / 转码库，并通过 `Threads::Threads` 链接线程支持。升级 baseline 并确认上游 port 已支持对应平台后移除此覆盖。
- `sdl3` / `bgfx`：Android 使用共享 SDL3 和 Vulkan，SDL3 安装同版本 Java 入口源码，bgfx 补齐 Android 系统库链接；其他平台保留固定 baseline 的配置。
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

以上为 2026-10-28 的历史记录。引擎与编辑器的项目职责随后已调整，Android 验证见下节；Windows / Linux / iOS 窗口尚未实机验证。

### 编辑器职责拆分与 Android

2026-10-29：引擎直接启动，编辑器必须传入项目。macOS 完整 Debug / Release 构建及 CTest 各 17/17 通过，额外覆盖编辑器项目依赖、初始化失败回滚和上下文清理；两种配置的 `engine_app --frames 3` 与 `editor_app --project projects/minimal --frames 3` 均正常渲染并退出。24 张类图实际解析和渲染通过。

首次 Android 包（尚未包含 V8）的 ARM64 / API 28 Debug / Release 原生库均完成编译与链接，APK 打包成功。编译宿主为 macOS ARM64，使用 JDK 17、NDK 27.2.12479018 和 SDL3 3.2.24；Release APK 使用 NDK 去除调试符号，约 12 MB，Debug APK 保留符号，约 121 MB。两者为本机调试签名，签名校验和 16 KB ZIP 对齐检查均通过。

复用 Game 的 `GameDev` ARM64 / API 34 模拟器，以 SwiftShader 提供软件 Vulkan。最终 Debug / Release 包均完成无项目启动、Vulkan 尚未就绪时立即切后台并恢复、同一进程连续 5 轮前后台切换、返回键正常退出、传入 `frames=120` 后正常结束，以及一次启动操作在同一进程重新建立 Activity 和 Vulkan。最终日志没有崩溃、ANR、SurfaceLost、BufferQueue abandoned 或暂停未确认错误。

验证中修复了 SDL 默认 Activity 重建行为及 Surface 销毁与 bgfx 后台线程的竞态：平台宿主在暂停和销毁 Surface 前请求 SDL 主线程关闭渲染器，确认后再继续；恢复时按需重建 GPU 资源。同步失败会明确结束本应用进程，不继续使用失效 Surface。已同步 Android 宿主、窗口和渲染类图。

最终日志保存在忽略的 `build/android-validation-debug/verified/`、`build/android-validation-release/verified/`，汇总及 APK 哈希位于 `build/android-validation-summary.json`，截图位于 `build/minimal-android.png`；旧日志保留供问题追溯。未清空模拟器或其他应用数据，最终留有运行中的 Release 测试包。此结果不代表真机性能或所有设备兼容性。

该轮结果尚未覆盖 V8；V8 补充验证见下文。Android 真机、Windows / Linux / iOS 窗口尚未执行本轮验证，通用 APK 资源加载尚未实现。

### Android V8

2026-10-29：补齐 Android ARM64 的 V8 14.3.127 交叉编译，macOS ARM64 宿主工具与 Android 目标工具链分开，复用 APK 的 NDK 27.2.12479018。原生库实际链接 V8、ICU 和 Temporal；通过链接 map、ELF Build ID 和 APK 内产物核对，排除只登记依赖或仅调用版本 API 的情况。

含 V8 的 Debug APK 为 135,333,009 字节，Release APK 为 65,962,354 字节，二者的签名校验与 16 KB ZIP 对齐检查通过。Debug 包保留断言与可调试属性，调试符号留在宿主的原始库中；初次携带完整符号的包安装空间不足，改为只剥离 APK 内符号后成功安装，未重编 V8。

在复用的 `GameDev` ARM64 / API 34 模拟器上，两种 APK 的 JavaScript、SharedArrayBuffer Atomics 及十六进制 / Base64、Intl、Temporal 四组测试全部通过。ICU 数据从 APK 复制到应用私有目录后参与实际执行；源码产物、暂存目录和 APK 中的 9,242,032 字节数据 SHA-256 一致，Debug 的设备私有文件哈希也一致。Release 不允许 `run-as`，未读取其私有文件做额外哈希校验。

两种配置的普通启动均通过 Vulkan 窗口、初始化期间切后台并恢复、5 轮前后台、返回退出、120 帧退出，以及完成 Activity 销毁后在同一进程重新启动的检查；最终日志没有 native crash、ANR、SurfaceLost、BufferQueue 或暂停握手错误。安装验证仅替换或卸载本项目测试包，未清空模拟器或其他应用数据。

本轮最终 macOS Debug / Release 完整构建及 CTest 各 17/17 通过，两种配置的引擎与编辑器入口也均完成 3 帧渲染后正常退出。Android 原子兼容分支另在模拟器验证了各宽度 CAS 成功与失败、并发累加、release / acquire 发布，以及非对齐 Base64 数据往返。Ninja 归档路径解析另以真实归档验证空格、美元符号、冒号及组合转义，合并与缓存复用通过。

Android 两种配置的重复 prepare / build 均命中缓存，各阶段约 1 秒；桌面两种配置的重复构建约 0.7 秒。缓存核对覆盖归档、导出头文件、GN 参数、Ninja 日志、成功标记及最终产物，确认文件内容、大小和时间戳均未变化。日志与逐文件快照保存在忽略的 `build/cache-validation/`。

APK 验证证据保存在忽略的 `build/android-v8-validation/debug-verified/` 和 `release-verified/`，包含 APK 哈希、链接 map 摘录、ELF 信息、ICU 哈希、逐项运行日志和截图，汇总为 `build/android-v8-validation/summary.json`。此轮尚未验证 Android 真机或 Linux / Windows 构建宿主；`ScriptSystem` 的玩法脚本接口仍属后续工作。

### 桌面脚本、日志、资源读取与同步

2026-10-29 在 macOS ARM64、AppleClang 21 上，通过继承标准 `ninja-engine-debug` / `ninja-engine-release` 的本机 Presets 完成构建，开启禁异常、编译警告视为错误及真实 GPU 测试；两种配置 CTest 均为 22/22 通过，V8 准备命中已有缓存。

- SDL3 同步读取覆盖二进制、文本、空文件、中文路径、嵌入 NUL、缺失文件、目录及超限文件；失败清空输出。
- 日志验证实际写盘、并发记录、轮换、失败后重试和重复生命周期。脚本验证真实 V8 模块执行、日志与路径绑定、缓存优先及内置回退、删除清单、语法或链接失败、重启和入口切换。
- 工具测试实际调用 TypeScript 5.9.3 与 MCP，覆盖中文及空格路径、编译失败保持旧产物、版本发布、删除、协议错误和热重载控制。MCP 标准输出保持 JSON-RPC。
- Debug 编辑器在独立项目上运行 3600 个 Metal 帧：启动玩法后通过 MCP 同步修改并重载，关闭重载后同步保留旧实例，重新开启应用待运行版本，TS 编译失败时保留原版本；三次玩法实例均执行关闭回调，编辑器正常退出。
- 35 张 Mermaid 类图均实际解析、渲染成功；C++ 格式与 `git diff --check` 通过。端到端记录保存在忽略的 `build/editor-script-validation/`。

本轮新增能力尚未在 Windows / Linux 或 Android 上实机验证；Android 接入按约定等待桌面审核。本轮没有触发 CI，也没有把完整资源批次同步表述为真机或网络传输。

### 运行时、编辑器与插件目录分层

2026-10-29 在同一 macOS ARM64 环境完成目录迁移验证：Debug / Release 构建成功，CTest 各 22/22 通过；两种配置的编辑器均读取新的 `include/engine.d.ts` 编译示例，启动玩法、渲染 3 帧并执行关闭回调后退出 0。现有脚本测试同时验证中文应用目录与原生路径绑定。

引擎头文件的作用域前缀与实际文件逐项核对通过，53 个受影响 C++ 文件符合格式规范；41 份 Markdown 中 198 条本地链接有效，38 张 Mermaid 图实际解析、渲染通过。运行记录位于忽略的 `build/scope-layout-validation/`。本轮仍复用依赖与 V8 缓存，未执行 Windows / Linux / Android 实机验证或触发 CI。

## 目录

所有受版本管理的目录均使用小写；`CMakeLists.txt` 等标准文件名保持原样。

```text
.
├── .github/workflows/  # Windows、Linux、macOS 的构建与测试 CI
├── cmake/              # 目标级编译设置
├── docs/               # 总说明及 runtime/editor/plugins 分层文档与类图
├── include/engine.d.ts  # 运行时虚拟 engine 模块的类型声明
├── include/engine/     # runtime/editor/plugins 三层公开头文件
├── platforms/          # 桌面路径实现及 Android 入口、Gradle 与原生打包配置
├── projects/minimal/   # settings 中的 TS 配置、assets 源码和 cache 生成目录
├── references/         # 本地参考文件，内容不纳入 Git
├── scripts/            # 宿主安装、TS 构建、MCP 客户端与 Android 打包工具
├── src/engine/         # 与 include/engine 对应的三层实现
├── tests/              # C++ 与命令行测试
└── third_party/        # vcpkg 包查找及 Git 子模块
```

## 构建选项

| 选项 | 顶层默认值 | 用途 |
| --- | --- | --- |
| `ENGINE_BUILD_APP` | `ON` | 构建引擎宿主；启用编辑器时另建 editor_app；Android 输出原生共享库 |
| `ENGINE_BUILD_TESTS` | `ON` | 构建本项目测试，同时受 `BUILD_TESTING` 控制 |
| `ENGINE_USE_COMPILER_CACHE` | `ON` | 自动使用已安装的 ccache / sccache，支持 Makefiles / Ninja |
| `ENGINE_REQUIRE_COMPILER_CACHE` | `OFF` | 缓存不可用时配置失败；ninja-* Preset 开启 |
| `ENGINE_WARNINGS_AS_ERRORS` | `OFF` | 本项目目标将编译警告视为错误，CI 开启 |
| `ENGINE_ENABLE_RUNTIME_DEPS` | `OFF` | 编译运行时系统与依赖；debug/release Preset 开启 |
| `ENGINE_ENABLE_EDITOR_DEPS` | `OFF` | 编译编辑器系统与专用依赖 |
| `ENGINE_ENABLE_ASSET_PIPELINE_DEPS` | `OFF` | 编译资源管线系统，查找并验证离线工具 |
| `ENGINE_ENABLE_GPU_TESTS` | `OFF` | 注册需要图形桌面和真实 GPU 的窗口测试 |
| `ENGINE_ENABLE_V8` | `OFF` | 启用显式准备的 V8 monolith、构建目标与嵌入测试 |
| `ENGINE_ENABLE_SCRIPT_RUNTIME` | `OFF` | 桌面 V8 玩法、日志/路径绑定与资源同步；完整 engine 预设开启，Android 不启用 |

作为 `add_subdirectory()` 子项目使用时，应用与测试默认关闭。只使用系统基类时链接 `engine::core`；使用完整引擎入口时启用运行时并链接 `engine::runtime`。各系统也提供单独目标，例如 `engine::project`、`engine::render`；目标统一传递 `include/engine/` 搜索根，调用方使用 `runtime/`、`editor/` 或 `plugins/` 开头的头文件路径。Preset 用于独立开发，会显式开启对应系统与测试。

`references/` 用于本地放置参考文件，内容不纳入 Git。部分实现代码可参考该目录，但文档中不得记录其中的具体内容。

详细说明见 [开发与第三方依赖](docs/development.md)，协作约定见 [AGENTS.md](AGENTS.md)。项目采用 [MIT License](LICENSE)。


2026-10-29 Android 脚本接入验证（macOS ARM64 宿主，复用 GameDev ARM64 / API 34 模拟器）：

| 范围 | 实际结果 |
| --- | --- |
| Android Debug / Release 原生库和 APK | 开启完整脚本运行时与严格警告，成功构建；V8 命中各自编译缓存，编辑器和 MCP 不进入 APK |
| 桌面 Debug / Release 回归 | CTest 各 24/24 通过，含新增 Android 资源打包与模拟 adb 故障测试 |
| Debug APK 的真实设备通路 | 包内 Gameplay 与日志启动、缓存优先、同进程 A→B 重载、关闭期间待应用、开启后切换、非法 TS 保留版本均通过 |
| 包内回退与删除 | 未覆盖模块从 APK 读取；删除标记拒绝回退且旧实例保持运行；首次同步将删除的包内文件列入清单 |
| 生命周期与持久数据 | 三轮后台/前台切换保持同一进程，重启保留同步版本，ICU 内容相同时复用缓存 |
| 最终 APK 运行 | Vulkan 画面正常；Debug 和 Release 的 V8 / Intl / Temporal 测试均通过；Release 玩法运行 120 帧正常退出，MCP 明确拒绝非调试包 |
| 类图与文档 | 40 个 Mermaid 图实际渲染成功，200 条本地 Markdown 链接有效 |
| 模拟 adb 故障回归 | 二进制/中文/引号路径、传输失败、哈希不符、锁冲突、发布回执丢失、APK 更新后的删除基线均覆盖 |

本轮设备验证使用模拟器；尚未在实体 Android 手机运行。版本清单中的 FNV-1a 哈希用于完整性校验，不作为发布签名。当前 adb 同步仅用于调试包，Release 的发布签名和正式内容更新渠道未接入。
