# Engine

Engine 是一个 C++20 跨平台游戏引擎，使用 CMake 与 vcpkg。`Engine` 管理系统生命周期，`System` 支持子系统，`RuntimeHost` 装配运行时并驱动 SDL3 窗口与 bgfx 渲染。`engine_app` 可以显示空白窗口，并处理缩放、暂停、恢复及退出；`editor_app` 默认空白待机，由 MCP 启动项目，在同一窗口绘制节点组件场景中的 PBR 方块，停止后恢复空白。

## 功能与架构

模块职责与使用范围分别记录于以下文档：

- [引擎架构总览](docs/architecture.md)：模块职责、依赖方向与说明入口。
- [资源系统](docs/class-diagrams/index.md#资源系统)：完整编辑器目录、精简运行清单、同步按需加载与依赖释放。
- [首个游戏](docs/runtime/first-game.md)：割草射击游戏的核心玩法循环。

架构以[类图索引](docs/class-diagrams/index.md)为评审入口。`include/engine/` 与 `src/engine/` 按 `runtime/`、`editor/`、`plugins/` 对应组织；运行时各系统继续独立成目录，公共工具放在 `runtime/utils/`。自有头文件从层级名开始包含，例如 `runtime/resource/resource_system.hpp`、`editor/editor_system.hpp`、`plugins/mcp/mcp_system.hpp`，搜索根由目标传递。`Engine` 只管理抽象根系统，不直接依赖窗口、渲染、脚本或项目模块。`RuntimeHost` 负责系统装配、窗口与帧调度，`GameplaySession` 负责玩法会话及场景资源清理；世界持有物理、导航、动画、运行时 UI 及可选脚本子系统。引擎启动不需要项目，项目是编辑器宿主的启动上下文。

模块文档及独立类图也按 `docs/runtime/`、`docs/editor/`、`docs/plugins/` 分层；统一评审入口与跨层引用关系保留在 `docs/class-diagrams/`。项目、资源管线和资源同步归编辑器层，MCP 归插件层；它们可以使用运行时能力，运行时不反向依赖它们。

各系统已接入相应依赖的头文件和实际库调用，Debug / Release 链接测试会实例化所有启用的系统。项目、编辑器和资源管线是独立宿主模块，运行时不依赖它们。已实现网格资源加载与直接光照 PBR；场景中的全部对象使用 Node + Component 表达，相机与灯光同样是节点组件。场景、网格、材质和 Shader 已支持资源文件导入与运行时二进制加载；物理模拟、UI 绘制及完整编辑器界面尚未实现。桌面编辑器已接入 V8 ES 模块、项目玩法入口、默认写盘日志、完整资源版本同步和引擎热重载；使用方式见下方“桌面脚本与同步”。

Android 已提供 ARM64 原生构建和 APK 打包入口，平台资源集中在 `platforms/android/`，默认启用完整 V8 脚本运行时。APK 运行打包项目的 Gameplay，支持默认写盘日志、持久资源版本优先及包内资源回退；桌面 MCP 可通过 adb 同步到 Debug 包并控制引擎热重载。资源目录 v2 的打包与同步已接入；APK 与模拟器的既有验证对应目录重构前版本，详见下方验证范围。项目启停工具用于桌面编辑器，Android 宿主直接启动打包 Gameplay。

仓库提供示例项目 `projects/minimal/`：TS 配置、package 与锁文件放 `settings/`，TS 源码和其他资源放 `assets/`，每个资源旁有唯一 GUID 的 `.meta`，生成 JS 与二进制资源统一放 `cache/gen/<完整 GUID>`，没有后缀。示例入口为 `assets/scripts/game.ts`；缓存产物及 `node_modules/` 不提交。

`engine_app` 使用默认引擎配置即可启动，不接受 `--project`。`editor_app --project <path>` 必须传入项目根目录；编辑器宿主先通过 `ProjectSystem` 检查根目录及 `settings/`、`cache/`、`assets/` 三个子目录，缺失或类型不是目录就返回失败，不创建窗口；检查通过后才准备脚本并启动引擎及借用项目的 `EditorSystem`。`ProjectSystem` 自身仍只负责目录检查，TS 设置由资源管线读取。资源描述通过 toml++ 无异常解析，失败用返回值报告。TaskSystem 使用 Taskflow 执行导入任务，`settings/import.toml` 的 `import_threads` 默认 4，可设为 1–64。

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

推荐使用 `ninja-debug` / `ninja-release` Preset，构建窗口运行时（不含 V8），三端统一使用 Ninja 并要求可运行的编译缓存工具。`debug` / `release` 使用平台默认生成器。按需启用编辑器、资源管线或 V8；`editor-deps` 同时启用运行时与资源管线。

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

这两个 workflow 包括显式 V8 准备步骤，并启用桌面脚本运行；先按下节准备 Node.js、npm、Python 3 和示例项目的 TypeScript 依赖。已准备过且依赖未变时，可用 `cmake --preset ninja-engine-debug`、`cmake --build --preset ninja-engine-debug`、`ctest --preset ninja-engine-debug` 分步复跑。`engine-debug` / `engine-release` 提供相同系统组合，使用平台默认生成器。启用脚本运行的应用及测试会自动将 `icudtl.dat` 部署到可执行文件旁。

依赖安装缓存位于 `build/dependencies/`，按 `runtime`、`editor`、`asset-pipeline`、`base` 四组 feature 隔离；同组 Debug / Release 共用安装目录，避免重复安装。vcpkg 自带二进制缓存继续生效，并保留用户设置的 `VCPKG_BINARY_SOURCES`。删除某个应用构建目录不会清掉这些共享依赖；只修改引擎源码时，无需运行清理依赖或重新准备依赖的命令。

CMake 通过 `CXX_COMPILER_LAUNCHER` 等目标属性接入缓存，覆盖引擎、自有测试与 ozz；Windows 优先查找 sccache，其他宿主优先查找 ccache，也可用 `ENGINE_COMPILER_CACHE_PROGRAM` 指定可执行文件。已有 `CMAKE_CXX_COMPILER_LAUNCHER` 设置优先保留。Windows 缓存目标使用 `/Z7` 将调试信息放入对象文件，避免共享编译 PDB 阻止命中。用 `ccache --show-stats` 或 `sccache --show-stats` 查看实际命中。

该机制适用于 Makefiles / Ninja，Visual Studio 生成器不支持此启动器属性；`ninja-*` 预设要求缓存工具存在且可执行，缺失时提示运行环境安装脚本。需要显式关闭时同时传入 `-DENGINE_REQUIRE_COMPILER_CACHE=OFF -DENGINE_USE_COMPILER_CACHE=OFF`。配置缓存 `CMakeCache.txt` 不等于对象文件缓存，Debug / Release 的不同编译选项仍分别缓存。对应机制见 [CMake 启动器文档](https://cmake.org/cmake/help/latest/prop_tgt/LANG_COMPILER_LAUNCHER.html)和 [sccache 的 CMake / MSVC 说明](https://github.com/mozilla/sccache#usage)。

ozz 的源码副本仅在固定提交、覆盖文件或准备脚本变化，以及副本缺失时更新；内容相同的文件保留时间戳，无变化构建不重复运行准备。V8 使用独立 GN 工具链及成功构建缓存，不经过 CMake 的编译启动器：校验固定源码、补丁、平台 / SDK / 工具链和 GN 参数后，Debug / Release 分别复用已验证的库和头文件；命中时跳过同步、GN、Ninja 与归档合并。缺失产物或输入变化会使缓存失效，失败的构建不会留下成功标记。V8 自带的固定版本 Ninja 保持独立，不替换为环境安装的版本。

第三方本地补丁放在 `third_party/`，构建前的覆盖流程见[开发与第三方依赖](docs/development.md)。

依赖维护需保留以下兼容约束：

- `basisu` overlay 补充 macOS ARM64 与 Android 支持，Android 保留编码 / 转码库并关闭宿主工具；上游 port 提供等效支持后才能移除覆盖。
- Android SDL3 使用共享库并安装同版本 Java 入口，bgfx overlay 传递所需 Android 系统库，避免 Java / JNI 或链接依赖不一致。
- macOS TBB 使用共享库，防止 USD 动态库重复嵌入独立调度器；任务内存释放补丁回补 oneTBB 提交 `132268ddd04d74e8b2e21e431a7e770761def49f`。升级包含该修复的版本后可移除补丁，共享链接策略须单独保留或验证替代方案。
- USD 插件资源在 Windows 位于对应配置的 `bin/usd/*/resources/`，macOS 位于 `lib/usd/*/resources/`；测试按构建配置设置 `PXR_PLUGINPATH_NAME`。TBB / USD 回归同时检查数据保存、加载和正常退出。

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

下载、源码副本、Git 配置和编译产物均位于忽略的 `build/v8/`，不会修改 `third_party/v8/` 或用户全局 Git 配置。Windows 源码副本应用仓库保存的兼容补丁，以匹配本项目 `/MDd` / `/MD`；关闭全局 allocator shim，保留宿主 CRT 分配器。桌面构建同时运行上游嵌入样例；`engine.v8` CTest 通过 CMake 的 `engine::v8` 目标执行 JavaScript，检查 Intl 格式化和 Temporal 日期及时区运算。Android 交叉编译只构建上游样例，执行验证放到目标设备上的 APK 测试中，不在宿主运行 Android 程序。嵌入 V8 的程序需要将 `ENGINE_V8_ICU_DATA` 指向的 `icudtl.dat` 复制到程序旁，测试目标已自动部署。使用 `symbol_level=0` 控制产物体积，Debug 保留 V8 断言但不生成完整调试符号。

GN 的 monolith 未收集隐式 Rust 归档。`cmake/package_v8.cmake` 按 GN 实际依赖清单额外合并 Rust 与 Clang 运行库，生成 `engine_v8.lib`（Windows）或 `libengine_v8.a`；macOS 先从 Clang 通用运行库提取本机架构的副本，再合并归档。`engine::v8` 自动使用这个完整嵌入库，并在 macOS 传递 Foundation、CoreFoundation 和 Security 框架依赖。原 GN 库和上游样例保留，Temporal 功能未禁用。

macOS 的 `macos-system-lto-library.patch` 与 `macos-system-math.patch` 适用于固定 Chromium build 提交 `81330a6e45719ab8c0d22e5548afbedb5482b068`，`macos-signpost-attribute.patch` 适用于固定 V8 源码。同步依赖前撤回已知补丁，同步后重新应用；缓存命中时不改写源码。升级工具链后，分别验证 LTO 库选择、SDK 数学宏和 signpost 属性，再移除对应补丁，不能只因版本变化而删除兼容处理。

V8 各平台的验证范围与限制见下方“验证范围与限制”。

### 窗口与编辑器

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

Ninja 预设写入独立的 `build/ninja-debug/`、`build/ninja-release/`，避免与旧生成器的构建目录冲突。`debug` / `release` 预设采用平台默认生成器，保留在 `build/debug/`、`build/release/`；Windows 通常为 Visual Studio 工程，Linux / macOS 通常为 Makefiles。

程序目标名为 `engine_app`。Linux / macOS 的 Ninja 预设下运行：

```sh
./build/ninja-debug/engine_app
# 渲染 120 帧后自动退出，适合冒烟验证。
./build/ninja-debug/engine_app --frames 120
```

Windows 的 Ninja 预设对应 `.\build\ninja-debug\engine_app.exe`；Visual Studio 预设对应 `.\build\debug\Debug\engine_app.exe`。macOS / iOS 使用 Metal，Windows 使用 DirectX 12，Android / Linux 使用 Vulkan；所选后端不可用时返回失败，不以 Noop 或其他后端回退代替成功。

编辑器宿主由 `editor-deps` 或 `ninja-engine-debug` 等启用编辑器的预设提供，启动时传入项目：

```sh
./build/ninja-engine-debug/editor_app --project projects/minimal
```

该入口验证项目并建立引擎、ImGui 上下文；完整引擎预设还会构建并同步项目脚本，等待 MCP `start_project` 后运行。尚未绘制完整编辑器界面。

普通 CTest 不要求桌面环境。要在有图形桌面的机器运行真实窗口/GPU测试：

```sh
cmake --preset ninja-debug -DENGINE_ENABLE_GPU_TESTS=ON
cmake --build --preset ninja-debug
ctest --preset ninja-debug
```

`engine.window_render` 验证真实后端、窗口生命周期、方块提交、场景切换及重复启停后实际 bgfx 资源回到待机基线。`editor.project_lifecycle` 使用真实编辑器、TypeScript 编译器和 MCP，验证空白待机、启停、重载和失败清理。`editor.mcp_commands` 验证场景查询、组件修改、批量回滚、撤销与会话隔离。这些窗口测试仅在 `ENGINE_ENABLE_GPU_TESTS=ON` 时注册并串行执行；命令执行、协议注册和文件传输测试无需 GPU。

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

编辑器先导入 `assets/`：读取 `.meta` 身份、解析资源描述、编译 TS 与目标平台 Shader，并生成 `cache/gen/<完整 GUID>`。SQLite 在 `cache/assets.sqlite` 记录输入哈希、依赖、工具版本与生成版本；再次导入只转换变化及受影响资源，失败保留上一版完整输出。随后建立本会话不变的内置资源快照并发布持久资源版本，全部引擎系统就绪后保持空白，等待 MCP 的 `start_project`。启动时示例 Gameplay 调用 `Scene.Load("project/scenes/main.scene")`，从 FlatBuffers 缓存恢复方块、相机和方向光三个节点，停止时释放场景及本次运行 GPU 资源。

编辑器运行时，在另一终端修改 `projects/minimal/assets/scripts/game.ts`，然后执行：

```sh
python3 scripts/mcp_client.py --server build/ninja-engine-debug/mcp_app --project projects/minimal --runtime-dir build/editor-runtime status
python3 scripts/mcp_client.py --server build/ninja-engine-debug/mcp_app --project projects/minimal --runtime-dir build/editor-runtime start
python3 scripts/mcp_client.py --server build/ninja-engine-debug/mcp_app --project projects/minimal --runtime-dir build/editor-runtime sync
python3 scripts/mcp_client.py --server build/ninja-engine-debug/mcp_app --project projects/minimal --runtime-dir build/editor-runtime hot-reload on
python3 scripts/mcp_client.py --server build/ninja-engine-debug/mcp_app --project projects/minimal --runtime-dir build/editor-runtime status
python3 scripts/mcp_client.py --server build/ninja-engine-debug/mcp_app --project projects/minimal --runtime-dir build/editor-runtime hot-reload off
python3 scripts/mcp_client.py --server build/ninja-engine-debug/mcp_app --project projects/minimal --runtime-dir build/editor-runtime stop
```

Windows 的 `--server` 使用对应 `mcp_app.exe`。客户端通过 stdio 启动 `mcp_app`，完成 MCP 初始化并调用一次工具；也可将这个可执行文件及相同参数配置到其他 MCP 客户端。`sync` 同时导入项目资源和同步完整版本；热重载开关只写控制请求，切换由引擎在帧边界执行。同步成功或返回 `pending engine decision` 不等于新资源版本已经启用，应通过 `status` 的 `activeRevision`、`availableRevision`、`running` 和 `lastError` 确认。运行资源版本是引擎最近一次上报；编辑器另有带心跳的项目状态，超过 5 秒无更新会标记离线。

热重载默认关闭；同一持久目录已有控制文件时读取其设置。关闭期间仍可同步新版本，当前实例和模块快照保持不变；开启后应用最新完整版本。候选脚本编译或静态链接失败时旧实例继续运行；新模块执行或 `OnInitialize` 失败会停止此次玩法并报告错误，不承诺撤销副作用或保留任意 JS 状态。`Update` 异常会记录错误、结束本次 Gameplay 并清理场景，编辑器回到待机。停止项目后，同步或开启热重载不会自动启动项目。

运行 SDK 位于 [engine.d.ts](include/engine.d.ts)，由虚拟 ES 模块 `engine` 提供：

```ts
import { Debug, Gameplay } from "engine";

export default class Game extends Gameplay {
    override OnInitialize(): boolean {
        Debug.Log("Game started");
        return true;
    }

    override Update(): void {}

    override OnShutdown(): void {
        Debug.Log("Game stopped");
    }
}
```

每次玩法运行只有一个项目入口，可直接承载业务；它独立于节点。`OnInitialize` 必须返回布尔值。`Debug.Log`、`Debug.LogWarning`、`Debug.LogError` 接受字符串并映射到原生日志。`Scene.Load(path)` 从当前目录按需加载场景及必要依赖，`Scene.FindNode(name)` 查找节点；`Scene.Create` 可以动态创建所属玩法会话的场景；`Scene.CreateNode` 创建节点，节点可以添加 `Camera`、`Light`、`MeshRenderer` 组件。全部场景对象都遵守这个结构。节点管理父子关系，Transform 提供局部位置、旋转与缩放；`Time.deltaTime` 为当前 Update 的秒数。完整用法见 [示例 Game](projects/minimal/assets/scripts/game.ts) 与 [SDK 声明](include/engine.d.ts)。停止或重载后旧句柄失效，失败返回 false/null。

`App` 的静态只读路径具有以下含义：

| 脚本接口                 | 用途                                                                      |
| ------------------------ | ------------------------------------------------------------------------- |
| `App.builtinAssetsPath`  | 内置资源根目录；编辑器中映射项目转换缓存，真机为包内资源                  |
| `App.persistentDataPath` | 持久数据根目录，保存同步版本、日志、控制与状态；可由 `--runtime-dir` 指定 |
| `App.cachePath`          | 平台临时缓存目录，可重建；不承载需要跨启动保留的同步版本                  |

`LogSystem` 默认将日志写入 `persistentDataPath/logs/engine.log` 并通过 SDL 输出到平台日志（桌面 stderr、Android logcat）；单文件达到 4 MiB 时轮转，保留 3 个旧文件。项目编译输出 `cache/gen/` 与平台临时 `App.cachePath` 是不同目录。

持久版本保存在 `persistentDataPath/assets/versions/<revision>/`，只含 `catalog.bin` 和其引用的散文件或合并包，SQLite 不进包。全部内容核对大小及 FNV-1a 64 哈希后，才原子发布 `assets/available.json`。运行开始先选择完整持久版本，未发布时使用内置版本；运行模块和场景持有实际加载资源的共享引用；核心目录只保存元数据，不预读所有实体。TS 相对 import 通过清单中的模块路径映射到 GUID，已删除模块不会回退到旧包体。绝对路径、越界路径和越界符号链接会被拒绝。

文件读取统一通过 SDL3 同步接口，单实体上限 64 MiB；资源目录最多 65536 项，合并包按成员范围读取，不要求整包进入内存；脚本源码及部署控制 JSON 文本另有 4 MiB 上限，单次模块图最多 256 个模块。运行时支持相对路径静态 ES import 和 `engine` 模块，不提供动态 import、顶层 await 或 Node.js API，也不自动打包任意 npm 运行时依赖。TS 的相对 import 应使用生成后的 `.js` 路径。

MCP 同步整个资源目录，包括场景、Mesh、材质、Shader、脚本及其他普通资源；按 GUID 将完整版本发布到本机或 Android Debug 包的持久目录，通过 `script-control.json` / `script-status.json` 控制引擎；不提供独立网络服务或差量同步。`ENGINE_ENABLE_SCRIPT_RUNTIME` 在完整桌面和 Android 预设均开启；关闭时保留 V8 版本探针。Android 传输使用 adb，具体命令如下。

### 渲染与项目控制

- 同一编辑器窗口在空白与项目画面间整体切换。场景、节点和组件归当前运行；共享窗口与图形设备保留到编辑器关闭。停止会清除最后一帧，重复启动不会创建第二套实例。
- 示例使用 `.mesh` 文件中的 24 顶点、36 索引方块，运行时按导入网格创建 GPU 缓冲；支持无光照材质与基础色/金属度/粗糙度的直接光照 PBR。光照采用 GGX、Smith、Schlick；最多 8 盏方向/点/聚光灯，视角来自活动透视或正交 Camera 节点；基础色、法线及 ORM 贴图由 BasisU 缓存提供。尚未实现 IBL 环境贴图、阴影和透明混合。
- 内置 Shader 使用宿主 shaderc 在构建时编译为数组，项目 `.shader` 描述引用独立源码 GUID，由导入器编译为目标平台二进制；runtime feature 也声明宿主 bgfx[tools] 依赖。目标后端固定为 Metal、DirectX 12 或 Vulkan，不依赖运行目录中的示例 Shader 文件。可用 `ENGINE_SHADERC_EXECUTABLE` 指定宿主工具，缺失或无法执行时配置失败。
- 桌面 MCP 提供无参数 `start_project`、`stop_project`（客户端简写 `start`、`stop`）。目标固定为当前 `--project` 和 `--runtime-dir`，须先启动编辑器。Android 工具提供同步、热重载和状态查询；Android 由平台宿主启动 Gameplay，不提供编辑器项目启停。
- `project-control.json` / `project-status.json` 位于持久运行目录。请求带会话与编号，由编辑器主线程执行；状态提供 `projectState`、`session`、`lastRequestId`、`lastRequestSucceeded`。场景和 GPU 数量查询分别属于场景与渲染系统，不从 Engine 转发。
- 启停等待最多 10 秒；超时的 `activation: pending` 表示尚未确认，**不是取消**。查询同一请求的最终状态后再操作。旧编辑器会话请求不会在新会话重放。
- 一个运行目录供一个编辑器实例使用；并行编辑器使用不同 `--runtime-dir`。MCP 通过 `project-control.lock` 串行化控制；若 MCP 被强制中止留下锁，先确认相关客户端已退出且状态中没有正在执行的请求，再移除该运行目录中的锁目录。不能删除其他活动操作的锁。

### MCP 模块注册与场景编辑

`McpSystem` 只处理协议、工具注册和参数检查。`ProjectMcpTools` 登记原有项目启停、资源构建、同步与状态工具；`EditorMcpTools` 登记桌面场景工具。各模块提供工具名称、参数模型和回调，宿主在 MCP 初始化前登记。新增模块不需要改协议分派函数；需要主线程操作的工具回调可复用 `EditorMcpTools::call_command`，由对应编辑器模块注册和执行命令。

| 工具                     | 参数与作用                                                                                            |
| ------------------------ | ----------------------------------------------------------------------------------------------------- |
| `get_scene`              | 无参数；查询活动场景、节点和组件，待机返回 `scene: null`                                              |
| `set_transform`          | `sceneId`、`nodeId`，以及至少一项 `position` / `rotation` / `scale` 三分量数组                        |
| `set_light`              | `sceneId`、`nodeId`，以及 `color` 四分量数组或 `intensity`                                            |
| `set_material_parameter` | `sceneId`、`nodeId`、`name`、`type`、`value`；类型为 `float` / `v2` / `v3` / `v4`，值为对应标量或数组 |
| `execute_batch`          | `commands` 数组，每项含 `command` 与 `arguments`；支持上述三种修改，最多 64 项                        |
| `undo` / `redo`          | `sceneId`；撤销或重做一条编辑记录                                                                     |
| `get_editor_receipt`     | `session`、`requestId`；查询原请求的持久回执                                                          |

场景与节点 ID 使用查询返回的十进制**字符串**，保留完整 64 位身份。修改只作用于已有组件和已声明的材质参数，失败返回原因；当前不提供创建几何体、修改拓扑或保存场景的 MCP 工具。编辑结果属于本次运行，不自动写回资源文件。

```sh
python3 scripts/mcp_client.py --server build/ninja-engine-debug/mcp_app --project projects/minimal --runtime-dir build/editor-runtime call get_scene
# edit.json 使用查询到的 sceneId/nodeId，内容例如：
# {"sceneId":"1","nodeId":"42","position":[1,0,0]}
python3 scripts/mcp_client.py --server build/ninja-engine-debug/mcp_app --project projects/minimal --runtime-dir build/editor-runtime call set_transform --arguments-file edit.json
```

通用 `call <工具名>` 接受 `--arguments '<JSON对象>'` 或 `--arguments-file <文件>`，二者互斥，省略时传空对象。示例 ID 只演示格式，实际操作必须使用当前查询值。

场景工具经 `editor-command.json` 交由编辑器主线程执行，MCP 不直接访问运行场景。每次成功修改生成撤销记录，批量修改失败按相反顺序回滚，成功批次只占一条记录；历史最多 128 条。停止、重启或切换场景后旧历史失效。玩法若已修改目标值，撤销会返回冲突，避免覆盖后来的业务状态；例如示例 Game 持续旋转方块时，旋转编辑可能产生此类冲突。

提交使用操作系统文件锁 `editor-command.lock`，进程退出自动释放；当前会话的待完成请求不会被覆盖。请求和回执均核对项目、会话及编号，单文件最多 1 MiB。等待 10 秒仍未确认时返回 `activation: pending` 及原请求身份，表示**结果未确认而非取消**。回执保存在 `editor-receipts/<session>/<requestId>.json`，最新快照为 `editor-receipt.json`；同一 ID 重放不重复执行。查询回执无需编辑器在线；查询成功不代表原修改成功，须检查其中的 `success` 和 `error`。`execute_batch` 的回执命令名为编辑器内部的 `batch`。

编辑器命令模块通过 `EditorCommands::register_command` 登记处理函数，修改返回一个实现 `EditorEdit::apply/revert` 的记录。宿主可借用 `EditorSystem::commands()` 在初始化后、进入主循环前登记模块；全部访问限于编辑器线程。运行时组件保持普通 C++ 接口，不引用 MCP、撤销系统或编辑器。对应类图见[命令执行](docs/editor/class-diagrams/editor-commands.md)及[编辑器 MCP 工具](docs/plugins/class-diagrams/editor-mcp-tools.md)。

### 组件、数据模型与资源扩展

- Node 只管理身份、父子关系和 `unique_ptr<Component>` 列表。C++ 使用 `add_component<T>()` / `get_component<T>()`；TS 使用 `AddComponent(Type)` / `GetComponent(Type)`。每个新节点自动具有 Transform，相机、灯光和网格也都是组件。
- `include/schemas/runtime/scene.fbs` 只描述场景节点和组件，网格/材质通过 GUID 引用。`mesh.fbs`、`shader.fbs`、`material.fbs`、`asset_catalog.fbs`、`resource_header.fbs` 独立维护对应模型，`common.fbs` 提供共享数值类型。构建使用宿主 flatc 生成 C++ 到 `build/<preset>/generated/include/engine/runtime/schema/`，生成 TS 到 `build/<preset>/generated/typescript/`；不提交生成物。可用 `ENGINE_FLATC_EXECUTABLE` 指定能在宿主运行的 flatc。FlatBuffers npm 依赖归项目 settings；场景以 `.scene` TOML 文件保存，导入为 FlatBuffers；运行时校验并读取 GUID 二进制。
- 材质和 Shader 的完整资源路径以 `builtin/` 或 `project/` 开头。内置 `builtin/materials/default-pbr` / `default-unlit` 分别引用 `builtin/shaders/pbr` / `unlit`；项目可导入自己的 Shader，也可让项目材质复用内置 Shader。两个空间互不覆盖，内置定义不依赖项目。停止项目释放场景、脚本资源引用和 GPU 句柄，最后一个使用者释放后项目数据自动回收；内置定义保留至引擎关闭。
- `mesh.material.SetType(path)` 选择材质定义，`SetShader(path)` 可为实例选择 Shader；`SetFloat/SetV2/SetV3/SetV4(name, ...)` 按声明匹配参数。未声明名字、类型错误、越界数值及未知路径均返回 false，失败保留原值。项目定义来自 `.material` / `.shader` 的导入二进制；这些资源与场景独立维护。运行时修改的材质参数属于当前实例，场景文件保存材质 GUID，不自动回写实例覆盖值。
- ScriptBinding 是绑定接口，各类的 JS 外观与原生回调在 `src/engine/runtime/script/bindings/` 分文件维护。新增 `xxx_binding.hpp/.cpp` 实现，再在 `script_binding_registry.cpp` 登记即可；该目录的绑定源文件由 CMake 自动纳入。公共 BindingContext 处理运行身份、失效引用和参数检查，ScriptSystem 保留脚本加载、生命周期和热重载职责。
- EditorEnvironment 借用已校验的项目，配置 App 路径并直接提交导入目录与挂载。ProjectSystem 不提供重复的 root_path 入口；运行时继续通过 App 读取路径语义。

### 资源导入与 GUID

```sh
./build/ninja-engine-debug/asset_build_app --project projects/minimal
```

渲染基础支持 UV0、切线、顶点色、子网格材质槽、动态网格提交、透视/正交相机及最多 8 个方向/点/聚光灯。PBR 的 GGX/Smith/Schlick 实现适配 The Forge `ShaderUtilities.h.fsl`，切线空间采样参考 bgfx `06-bump`；版权及许可保存在 `third_party/notices/`。不包含阴影、IBL、透明排序或 ProBuilder 建模算法。

`Material.SetTexture(name, path)` 按 Shader 的 sampler 声明设置贴图，`GetTexture(name)` 返回当前资源路径。内置参数为 `baseColorTexture`、`normalTexture`、`ormTexture`（AO/Roughness/Metallic）；法线与 ORM 使用线性贴图。`MeshRenderer.GetMaterial(slot)` 访问 0..15 的材质槽，`SetMaterialCount(count)` 设置槽数。C++ `set_mesh_data()` 深拷贝生成的 MeshDataT，成功递增修订号并更新 GPU；未落盘的动态网格不能保存成包含无效 GUID 的场景。

图片支持 PNG/JPG/JPEG。新图片的 `.meta` 自动生成贴图导入设置，示例：

```toml
guid = "<完整 GUID>"
importer = "texture"
meta_version = 1

[texture]
format = "auto"
compression = "uastc"
compression_level = 2
rdo_quality_scalar = 1.0
srgb = true
mipmaps = true
```

- `format`：`auto` 按 GPU 与转码器能力选择，或明确 `rgba8` / `bc7` / `astc4x4` / `etc2`；显式格式不支持时返回失败。
- `compression`：UASTC 或 ETC1S。UASTC 使用 `rdo_quality_scalar`（0.001..50，越小失真越少），ETC1S 改用 `quality`（1..255，默认128）。切换模式同时替换质量字段。
- `compression_level`：UASTC 0..4，ETC1S 0..5；`srgb` 区分颜色与线性数据，`mipmaps` 决定是否生成完整 mip 链。
- 设置、源图片、Schema 和编码器版本均参与增量键。输出为 `cache/gen/<完整GUID>` 中的 TextureData/BasisU 资源；Shader sampler 默认纹理和材质纹理引用进入统一依赖树、打包和同步。
- 旧图片若使用 `importer = "source"`，修改为 `texture` 时保留原 GUID；不自动改变已有原始二进制的用途。单边最大8192、总像素最多16M，解码前检查图片头。

BasisU 使用已有依赖的 C++ 编码接口，整体并行度由项目 `import_threads` 控制，每任务编码器使用单线程。`third_party/overlay-ports/basisu/quiet-library-output.patch` 修复上游静默模式仍输出压缩状态的问题，确保 MCP stdout 只承载协议。

Shader `.shader` 中 `[[samplers]]` 声明 `name/uniform/stage/default_texture/exposed`，默认纹理只允许内置资源；`stage` 必须与 Shader 源码 `SAMPLER2D` 的槽号一致。bgfx 公共反射能校验名称、类型与数量，不能反射槽号。材质 `.material` 的 `[[textures]]` 用 `name` 与 `texture` GUID 指定覆盖值。示例项目已加入棋盘贴图、UV 和完整 PBR sampler 声明。

编辑器 MCP 增加 `set_camera`、`set_material_texture`；`set_light` 支持 `kind/range/innerAngle/outerAngle`，材质命令支持可选 `slot`。场景查询返回全部材质槽和贴图；修改沿用批量事务、撤销/重做和完成回执。历史只记录资源身份与版本，不让停止项目后的资源因撤销栈继续常驻。

`assets/` 下场景、网格、材质、Shader 描述分别使用 `.scene`、`.mesh`、`.material`、`.shader`，内容为 TOML，不追加 `.toml`。TS、Shader 源码、图片等保留原格式。TS 增量指纹来自编译器实际读取的文件，包括继承配置、额外声明、标准库和编译器自身；导入前后再次核对。Shader 保守依赖项目全部普通源文件，确保自定义 `.inc` 等包含文件变化不会漏编译。每个文件的相邻 `.meta` 记录唯一完整 GUID；新资源自动生成，复制已有 meta 导致冲突时返回失败，移动资源必须一起移动 meta。已知资源丢失 meta 时返回失败，避免静默改变引用。

`cache/gen/` 的直接子文件名只允许完整 GUID，不带扩展名。`cache/catalog.bin` 使用 v2 格式，包含 GUID、逻辑路径、类型、运行依赖、实体头大小、内容哈希、目标平台与存储定位；`cache/assets.sqlite` 是可重建的编辑器索引。删除 SQLite 后根据源文件/meta 重新导入，GUID 不变。导入使用项目锁和暂存/回滚记录，失败不会发布半成品；无变化时保持正式文件与版本不变。

`GuidMap` 位于核心 `runtime/core/`，提供 `path_to_guid` / `guid_to_path`，编辑器与真机共享，不依赖 SQLite。逻辑路径以 `project/` 或 `builtin/` 开头；运行时从资源目录恢复映射。MCP 同步允许没有玩法脚本的纯资源目录；入口是否可运行由启动阶段检查。TaskSystem 默认 4 个导入线程，由 `settings/import.toml` 配置；任务失败会跳过依赖任务并向调用者返回失败。

`SceneImporter::write` 将节点数据写回 `.scene`，`read` 用于导入校验；运行时只读取 FlatBuffers。稳定节点 ID 与运行期实体 ID 分离，父子关系及活动相机在保存/加载后保持。编辑器尚未提供保存按钮，模型格式导入尚未实现；未配置专用导入器的原始文件按字节缓存。

### 资源运行包

```sh
./build/ninja-engine-debug/asset_build_app --project projects/minimal --manifest settings/resource-build.toml
# 可重复 --item；省略时选择清单的全部构建项。
./build/ninja-engine-debug/asset_build_app --project projects/minimal --manifest settings/resource-build.toml --item game
```

项目清单在 `settings/`，指定目标平台、`cache/build/` 下的输出位置、入口与显式保留项。构建按运行依赖计算闭包，支持 `storage = "files"` 的独立 GUID 文件和 `storage = "package"` 的未压缩合并包；统一 `catalog.bin` 为每个 GUID 保存一个实际位置。构建输入依赖仍归 SQLite 增量导入，Shader 源码与 TypeScript 声明不会因为参与编译而自动打进运行包。未变化的文件和包直接复用，失败保留上一份成功发布。

MCP 提供 `build_resources`，参数为 `manifest` 和可选的 `items` 数组。`sync_assets` 可传 `source`（例如 `cache/build/minimal`）同步已构建目录；省略时先导入并同步完整编辑器目录。同步成功只表示候选部署可用，是否激活由运行宿主与热重载开关决定。

命令行客户端使用同一工具协议：

```sh
python3 scripts/mcp_client.py --server build/ninja-engine-debug/mcp_app --project projects/minimal --runtime-dir build/minimal-runtime build --manifest settings/resource-build.toml --item game
python3 scripts/mcp_client.py --server build/ninja-engine-debug/mcp_app --project projects/minimal --runtime-dir build/minimal-runtime sync --source cache/build/minimal
```

编辑器直接使用 `cache/gen`，不要求打包。会覆盖该目录的导入或构建要求先 `stop_project`；运行中会返回明确错误。同步已有的独立运行包不覆盖该缓存，可以在关闭热重载时准备候选版本。`cache/gen` 同时只保存一个目标平台的产物，构建 Android 后要先重新导入宿主平台产物，再启动桌面项目。

资源核心只接收调用方准备好的目录和挂载，不扫描工程、不发现发布版本。加载顶级资源时自动加载并共享必要依赖，最后一个使用者释放时回收；读目录、读实体头、加载载荷是分开的操作。v1 目录需重新导入，不能直接作为 v2 运行包。接口与文件格式见[资源加载](docs/runtime/resource-loading.md)和[资源构建](docs/editor/resource-build.md)。

### Android APK

Android 平台工程位于 `platforms/android/`，沿用 [SDL3 的 Activity / JNI 框架](https://wiki.libsdl.org/SDL3/README-android)。原生部分仍由根 CMake Presets 编译，Gradle 只处理 Java、Manifest 和 APK；SDLActivity 源码从同版本 SDL3 的 vcpkg overlay 安装结果取得。APK 直接启动引擎，不包含编辑器项目目录、项目校验或编辑器 UI。

准备 Python 3、JDK 17、Android SDK Platform 35、Build Tools 36.1.0、NDK 27.2.12479018，以及上文的 CMake / Ninja / 编译缓存 / vcpkg。设置 `ANDROID_HOME`、`ANDROID_NDK_HOME`、`JAVA_HOME` 和 `VCPKG_ROOT` 指向已有安装；可复用其他工程的安装目录。Android Gradle Plugin 固定 9.0.0，wrapper 固定 Gradle 9.1.0 并核对官方 SHA-256；也可通过 `GRADLE_HOME` 复用相同版本，脚本会检查版本。

```sh
npm ci --prefix projects/minimal/settings
python3 scripts/build_android.py --config debug --project projects/minimal --asset-builder build/ninja-engine-debug/asset_build_app
python3 scripts/build_android.py --config release --project projects/minimal --asset-builder build/ninja-engine-debug/asset_build_app
```

包含 V8 的 Android 构建入口支持 macOS ARM64 / x64 和 Linux x64 宿主，Windows 宿主的 Android V8 工具链尚未接入。脚本依次调用 `android-arm64-debug` / `android-arm64-release` 的根 configure、显式 `-prepare`、build Preset，再打包。产物为 `build/apk/engine-arm64-debug.apk` 和 `build/apk/engine-arm64-release.apk`；最低 Android 9 / API 28，ABI 为 `arm64-v8a`，需要 Vulkan。两种 APK 均使用本机调试签名，供安装测试；发布签名尚未配置，签名密钥不进入仓库。

两种 APK 内的原生库均由 Gradle 去除调试符号以控制安装体积；Debug 保留断言与可调试属性。完整的 Debug 原生符号保存在 `build/android-arm64-debug/libmain.so`，可用 APK 原生库的 ELF Build ID 与之对应，无需把符号放进设备。

脚本检查 NDK 版本，并在打包前仅移除该配置的旧 APK，避免增量 ZIP 更新留下旧原生库空洞导致安装包膨胀；原生库、Java、Gradle 和依赖编译缓存继续复用。`--project` 指定打包项目，默认 `projects/minimal`，必须具有 `settings`、`assets`、`cache` 三个目录并安装 settings 中锁定的 TypeScript。根 CMake 的 bundle 目标通过指定的宿主 asset_build_app 增量构建资源：项目有 `settings/resource-build-android.toml` 时按该清单构建固定子集，读取构建回执的输出目录；没有该清单时沿用 `--target android` 导入模式。随后只校验并打包运行 catalog 引用的散文件或合并包；不复制 SQLite、TS/meta 或陈旧产物。`--asset-builder` 必须是已构建的桌面工具（Windows 文件名加 `.exe`），路径经 `ENGINE_ANDROID_ASSET_BUILDER` 传给 CMake；省略时只接受已有的 Android 目标 catalog，拒绝桌面 Metal 缓存和旧 `cache/assets`。原生库与上游缓存继续复用。`--skip-native` 仅重打包已生成的原生库及资源，不能同时指定 `--project` 或 `--asset-builder`。

Android 运行时包含 SDL3、bgfx、FlatBuffers、Basis Universal、flecs、Jolt、Recast / Detour、ozz、RmlUi 和 V8 的实际编译与链接。编辑器、toml++ 项目模块和离线资源管线不进入 APK。依赖共享安装在 `build/dependencies/android/`，与桌面安装目录隔离，并保留 vcpkg 二进制缓存。BasisU 的 Android overlay 只关闭宿主命令行工具，保留完整编码 / 转码库。

V8 使用独立的 `build/v8/android-arm64/` 工作副本和 Debug / Release 缓存，目标平台、NDK、API 与工具链均参与缓存身份，不复用桌面归档。宿主 Clang / Rust / GN 版本由 V8 固定，Android 使用同一 NDK 的 sysroot、compiler-rt 和 `libc++_shared.so`；关闭 ThinLTO，最终将完整嵌入依赖合并链接进 `libmain.so`。兼容补丁保存在 `third_party/overrides/v8/`，上游子模块保持只读：除 macOS 宿主和外部 NDK 配置外，NDK 27 缺失的 `std::atomic_ref` 由 V8 / simdutf 内部的 Clang 原子指令兼容分支处理，保持原有内存序与 NDK ABI，不混用其他版本的 C++ 标准库。

APK 包含 `assets/v8/icudtl.dat`、`assets/project/` 下的编译资源和 `assets/project-manifest.json` 完整清单；不包含项目设置、TS 源码或 Node.js。正常启动初始化 App 与 LogSystem，再由 RuntimeHost 初始化系统并启动项目 Gameplay；无需在手机上传入编辑器项目目录。GameplaySession 校验 APK 部署清单并向资源核心提交目录，将清单导出到持久目录供首次同步计算删除项；ICU 根据内容复用应用临时缓存中的副本，JS 直接通过 SDL 读取 APK。

可选 V8 嵌入测试，ICU 副本写入应用 `cache/v8-smoke/`，在新的 SDL 进程执行 JavaScript、SharedArrayBuffer 原子操作及十六进制 / Base64 转换、Intl，以及 Temporal 的日期和时区运算。固定 V8 版本的 Temporal 测试显式启用 `--harmony-temporal`；这不增加脚本系统的公开接口。

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

`emulator-5580` 为命令示例；使用前通过 `adb devices` 确认实际序列号并替换。已有进程要改用不同启动参数时，先用 `adb -s <serial> shell am force-stop org.engine.minimal` 停止本应用，再重新启动；不需要清除模拟器或应用数据。模拟器必须提供 Vulkan；软件 Vulkan 也可验证功能，但不能用来衡量真机 GPU 性能。

### Android 资源同步与热重载

| App 接口             | Android 指向                                                 |
| -------------------- | ------------------------------------------------------------ |
| `builtinAssetsPath`  | `apk:/assets/project`，由 ResourceSystem 读取 APK 的逻辑路径 |
| `persistentDataPath` | 应用私有 `files/`，保存日志、版本和控制状态，重启仍保留      |
| `cachePath`          | 应用私有 `cache/`，可重建的 ICU 等临时数据                   |

先启动 Debug APK，让引擎创建状态和内置清单。电脑上的 MCP 使用明确的设备序列号与包名，拒绝隐式选择设备：

```sh
python3 scripts/mcp_client.py --server build/ninja-engine-debug/mcp_app --project projects/minimal --android-serial emulator-5580 --android-package org.engine.minimal status
python3 scripts/mcp_client.py --server build/ninja-engine-debug/mcp_app --project projects/minimal --android-serial emulator-5580 --android-package org.engine.minimal hot-reload on
# 修改项目 assets 下的脚本、场景、材质等资源后同步；无需重新打 APK。
python3 scripts/mcp_client.py --server build/ninja-engine-debug/mcp_app --project projects/minimal --android-serial emulator-5580 --android-package org.engine.minimal sync
python3 scripts/mcp_client.py --server build/ninja-engine-debug/mcp_app --project projects/minimal --android-serial emulator-5580 --android-package org.engine.minimal status
```

adb 不在 PATH 时添加 `--adb <adb可执行文件>`。真机使用 `adb devices` 中的实际序列号；需要设备已经授权 adb 调试。MCP 运行在电脑，通过 `run-as` 写入该调试包的私有持久目录，手机不安装 MCP 服务。此通路仅支持 debuggable APK；Release 保持不可调试，同步请求会返回明确错误。

设备同步先上传暂存版本、读回验证大小与哈希，再改名为不可变版本，最后原子发布 `available.json`。发布过程使用设备目录锁；发布前失败保留旧的可用版本。最终发布回执丢失会报告未确认，此时应查询设备状态。每个资源版本必须包含完整的 `catalog.bin` 及其定位信息引用的散文件或合并包；运行时优先使用持久版本，没有持久版本时读取 APK 内置目录。同一次玩法运行固定使用一份完整 catalog，不能把不同版本的场景、材质和脚本混用；重载校验失败时保留原活动版本。删除资源在下一份完整 catalog 中不再出现，不会从 APK 意外恢复。可写版本在 `persistentDataPath`，不会因临时缓存清理而丢失。

热重载由引擎逐帧决定是否执行；关闭开关仍可同步文件，开启后加载最新版本。用 `status` 确认 `activeRevision` 与 `availableRevision` 相同且 `running=true`，同时检查 `lastError`。APK 更新或普通重启不会覆盖现有可用资源版本；要恢复包内资源，应先停止应用并由开发者明确清理对应同步数据。

## CI

CI 仅通过 `workflow_dispatch` 手动触发。在 GitHub 仓库的 Actions 页面选择 `ci`，点击 Run workflow 并选择分支；提交、推送、创建或更新 PR、合并分支均不会自动运行 CI。助手只有在用户明确要求运行 CI 时才可手动触发。

## 代码格式维护

格式整理覆盖仓库自有代码、测试、工具、平台入口，以及 third_party 中的自有 CMake 接入与 triplet。上游 Git 子模块、overlay portfile、移植的上游覆盖文件、补丁内容和构建生成物保持各自格式，不纳入此范围。语言规则由根 `.clang-format`、`.editorconfig`、`.cmake-format.json` 和 `pyproject.toml` 统一维护；C++ 使用 Allman 大括号、4 空格缩进及 100 列，并为控制语句保留大括号。

| 范围                                                                                      | 检查方式                                                                     |
| ----------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------- |
| `include/engine/`、`src/`、`tests/` 中的 C++ 与 `platforms/` 中自有 C++ / Java            | 将文件列表传给 `clang-format --dry-run --Werror`                             |
| `scripts/`、`tests/` 中的 Python                                                          | `python3 -m black --check scripts tests`                                     |
| 根 CMakeLists、`cmake/`、`tests/`、Android native_bundle、自有 third_party 接入与 triplet | 将这些自有 CMake 文件传给 `cmake-format --check`，排除 overlay、上游及生成物 |
| TypeScript / JavaScript                                                                   | 使用下方 Prettier 命令                                                       |
| 运行时和示例项目 Shader                                                                   | 按 `.editorconfig` 与现有 Shader 排版维护，并执行对应 Shader 编译            |
| `include/schemas/` 中的 FlatBuffers schema                                                | 4 空格、UTF-8、LF；通过 flatc 生成验证语法                                   |
| Android Gradle、Shell 与 PowerShell                                                       | 4 空格、UTF-8、LF、无行尾空白；保留各语言语法及现有行为                      |

```sh
prettier --check --tab-width 4 --print-width 100 include/engine.d.ts projects/minimal/assets/scripts/game.ts scripts/build_scripts.mjs tests/scene_schema_bridge.ts
```

Black 26.10.1、cmakelang 0.6.13 与 Prettier 用于格式维护，不是引擎构建依赖；可在独立工具环境安装。格式检查不能替代命名、模块职责、返回值错误处理及资源释放顺序的代码审查，C++ 修改仍需 Debug / Release 构建和受影响的 CTest。

## 验证范围与限制

以下为截至 2026-10-29 已完成的验证范围。资源目录 v2、依赖加载与清单构建在 macOS 验证；Android APK、模拟器与 Windows 的记录对应此次资源目录重构前版本。构建使用根 Presets 或继承它们的本机 Presets，未触发 CI。

| 环境                                                 | 已验证范围                                                                                                                            |
| ---------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------- |
| macOS ARM64 / AppleClang 21                          | 完整引擎 Debug / Release 构建及各 42 项回归通过；真实 Metal 画面、BasisU 贴图缓存、场景、脚本、模块化 MCP、主线程编辑与编辑器生命周期 |
| Android 打包与同步宿主工具                           | v2 目录、散文件和合并包的 Python 打包回归及模拟 adb 同步测试；尚未重新构建本轮 APK 并运行模拟器                                       |
| Android ARM64 / API 34 模拟器，软件 Vulkan（重构前） | Debug / Release APK 构建、安装、V8 / Intl / Temporal、PBR 场景、前后台切换、有限帧退出；Debug 全资源同步、热重载及跨进程持久版本      |
| Windows x64 / VS 2022                                | 依赖准备、Debug / Release 基础构建、ozz / USD 集成、bgfx 工具与 V8 嵌入测试；未覆盖完整编辑器和真实 DirectX 窗口                      |
| Linux / iOS / Android 实体设备                       | 未完成端到端运行验证                                                                                                                  |

桌面回归覆盖 Taskflow 依赖调度、导入线程设置、SQLite 增量与重建、重复 GUID、meta 缺失及移动、输入依赖变化、产物损坏重建、场景 TOML / C++ / TS FlatBuffers 往返。资源回归覆盖元数据目录、头部读取、按依赖共享与释放、持有资源时拒绝不兼容目录更新、散文件与合并包、清单子集及增量构建。另覆盖超过 64 MiB 合并包的按范围读取、同步和热重载，以及陈旧心跳下的活动缓存保护；模拟 adb 验证分块传输失败不发布。Metal 编辑器验证材质修改经资源同步实际改变画面、重复启停后 GPU 资源回到基线、停止恢复空白。

重构前的 Android Debug 验证包含 catalog 和 12 个 GUID 产物的逐字节、大小与哈希检查，覆盖场景、Mesh、材质、Shader、JavaScript、相对模块导入、图片、原始二进制及空文件。关闭热重载时新版本保持待应用；坏 TS、未知源 GUID 和损坏 catalog 被拒绝；重启保留已发布版本。Debug / Release 均验证前后台切换、正常退出及 V8 嵌入测试，Release 拒绝 `run-as`。这些结果不替代资源目录 v2 的设备验证。

V8、ozz、vcpkg 与编译缓存验证包含无变化构建不重建上游、不改写未变化文件，以及输入变化使缓存失效。Unix 环境安装模拟测试覆盖缺失工具、包管理器和失败退出；Windows PowerShell 模拟入口为 `tests/setup_windows_test.ps1`，尚未在 macOS 执行。跨平台脚本存在不等于全部平台已经完成实机测试。

MCP 模块化回归覆盖独立工具注册、参数检查、旧工具兼容、现有组件修改、批量回滚、撤销与重做冲突恢复、请求重放、超时后的持久回执、跨项目及跨会话隔离。批次中遇到不可撤销查询或未注册命令会恢复先前修改和历史。渲染扩展加入相机、灯光类型与具名贴图命令，真实编辑器测试验证修改、回执及撤销流程。

渲染基础使用 `local-ninja-debug` / `local-ninja-release`（继承根 Presets）完成构建，全套 CTest 各 42/42 通过。Metal 截图像素验证 UV、sRGB/线性、动态几何和三种灯光及累加；资源检查覆盖多材质槽、贴图共享和重复启停释放。贴图回归执行真实 PNG/JPEG 解码、UASTC/ETC1S 编码与转码，验证 meta 参数、完整 mip 链、增量命中、非法参数及失败保留旧缓存。Android 仅验证宿主打包和模拟 adb 同步，未重建本轮 APK，未验证各设备压缩格式矩阵。94 张类图通过 Mermaid 解析和渲染，验证结果的源码哈希与当前文档一致。

MCP 模块化阶段另已验证关闭 V8 和资源管线的编辑器 Debug / Release 构建及相关回归各 9/9；渲染基础阶段未重跑该精简配置。C++、Python 与 CMake 改动通过格式检查。

渲染基础本地证据保存在忽略的 `build/render-foundation-*.log` 与 `build/render-foundation-diagrams.json`。MCP 记录位于 `build/mcp-*.log` 与 `build/mcp-diagrams-final.json`，资源目录记录位于 `build/resource-*.log` 与 `build/resource-design-validation.json`；既有记录保留在 `build/refactor-*.log`、`build/refactor-module-validation/`、`build/guid-asset-validation/`、`build/android-guid-validation/`、`build/cache-validation/` 与 `build/local-deps/`。它们是验证产物，不是运行时依赖；仓库维护的复跑入口为 Presets 和 `tests/`。

限制：

- Android APK 均使用本机调试签名。16 KB ZIP 对齐检查通过，但 SDL3 的 ELF LOAD 仍为 4 KB 对齐，不能据此宣称完整支持 16 KB 内存页设备。
- adb 同步只支持 Debug 包；正式发布签名和内容更新渠道未实现。FNV-1a 哈希用于完整性检查，不提供签名认证。
- 软件 Vulkan 不反映真机性能；Android 实体手机、Linux / iOS 及完整 Windows 运行时仍需对应平台验证。
- 固定版本 bimg 的部分带 alpha 图片往返会因 `m_hasAlpha` 元数据不一致导致 `texturec --validate` 失败；已测样本像素一致，元数据兼容问题未修补。
- PBR 尚无阴影或 IBL；贴图导入支持 PNG/JPG/JPEG、UASTC/ETC1S、二维单图。物理模拟、动画播放、导航请求、游戏 UI 与完整编辑器界面未实现。

## 目录

所有受版本管理的目录均使用小写；`CMakeLists.txt` 等标准文件名保持原样。

```text
.
├── .github/workflows/  # Windows、Linux、macOS 的构建与测试 CI
├── cmake/              # 目标级编译设置
├── docs/               # 总说明及 runtime/editor/plugins 分层文档与类图
├── include/engine.d.ts  # 运行时虚拟 engine 模块的类型声明
├── include/engine/     # runtime/editor/plugins 三层公开头文件
├── include/schemas/    # FlatBuffers 跨语言资源数据模型
├── platforms/          # 桌面路径实现及 Android 入口、Gradle 与原生打包配置
├── projects/minimal/   # settings 中的 TS 配置、assets 源码和 cache 生成目录
├── references/         # 本地参考文件，内容不纳入 Git
├── scripts/            # 宿主安装、TS 构建、MCP 客户端与 Android 打包工具
├── src/engine/         # 与 include/engine 对应的三层实现
├── tests/              # C++ 与命令行测试
└── third_party/        # vcpkg 包查找及 Git 子模块
```

## 构建选项

| 选项                                | 顶层默认值 | 用途                                                                                          |
| ----------------------------------- | ---------- | --------------------------------------------------------------------------------------------- |
| `ENGINE_BUILD_APP`                  | `ON`       | 构建引擎宿主；启用编辑器时另建 editor_app；Android 输出原生共享库                             |
| `ENGINE_BUILD_TESTS`                | `ON`       | 构建本项目测试，同时受 `BUILD_TESTING` 控制                                                   |
| `ENGINE_USE_COMPILER_CACHE`         | `ON`       | 自动使用已安装的 ccache / sccache，支持 Makefiles / Ninja                                     |
| `ENGINE_REQUIRE_COMPILER_CACHE`     | `OFF`      | 缓存不可用时配置失败；ninja-* Preset 开启                                                     |
| `ENGINE_WARNINGS_AS_ERRORS`         | `OFF`      | 本项目目标将编译警告视为错误，CI 开启                                                         |
| `ENGINE_ENABLE_RUNTIME_DEPS`        | `OFF`      | 编译运行时系统与依赖；debug/release Preset 开启                                               |
| `ENGINE_ENABLE_EDITOR_DEPS`         | `OFF`      | 编译编辑器系统与专用依赖                                                                      |
| `ENGINE_ENABLE_ASSET_PIPELINE_DEPS` | `OFF`      | 编译资源管线系统，查找并验证离线工具                                                          |
| `ENGINE_ENABLE_GPU_TESTS`           | `OFF`      | 注册需要图形桌面和真实 GPU 的窗口测试                                                         |
| `ENGINE_ENABLE_V8`                  | `OFF`      | 启用显式准备的 V8 monolith、构建目标与嵌入测试                                                |
| `ENGINE_ENABLE_SCRIPT_RUNTIME`      | `OFF`      | V8 玩法、日志/路径绑定与引擎热重载；完整桌面 engine 及 Android 预设开启，同步工具仅构建在桌面 |

作为 `add_subdirectory()` 子项目使用时，应用与测试默认关闭。使用 Engine 与系统基类时链接 `engine::core`；使用 RuntimeHost 装配完整运行时时启用运行时并链接 `engine::runtime`。各系统也提供单独目标，例如 `engine::project`、`engine::render`；目标统一传递 `include/engine/` 搜索根，调用方使用 `runtime/`、`editor/` 或 `plugins/` 开头的头文件路径。Preset 用于独立开发，会显式开启对应系统与测试。

`references/` 用于本地放置参考文件，内容不纳入 Git。部分实现代码可参考该目录，但文档中不得记录其中的具体内容。

详细说明见 [开发与第三方依赖](docs/development.md)，协作约定见 [AGENTS.md](AGENTS.md)。项目采用 [MIT License](LICENSE)。
