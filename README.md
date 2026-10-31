# Engine

Engine 是一个 C++20 跨平台游戏引擎，使用 CMake 与 vcpkg。`Engine` 管理系统生命周期，`System` 支持子系统，`RuntimeHost` 装配运行时并驱动 SDL3 窗口与 bgfx 渲染。`engine_app` 可以显示空白窗口，并处理缩放、暂停、恢复及退出；`editor_app` 默认空白待机，由 MCP 启动项目，在同一窗口绘制节点组件场景中的 PBR 方块，停止后恢复空白。

## 功能与架构

模块职责与使用范围分别记录于以下文档：

- [引擎架构总览](docs/architecture.md)：模块职责、依赖方向与说明入口。
- [资源系统](docs/class-diagrams/index.md#资源系统)：完整编辑器目录、精简运行清单、同步按需加载与依赖释放。
- [首个游戏](docs/runtime/first-game.md)：割草射击游戏的核心玩法循环。

架构以[类图索引](docs/class-diagrams/index.md)为评审入口。`include/engine/` 与 `src/engine/` 按 `runtime/`、`editor/`、`plugins/runtime/`、`plugins/editor/` 对应组织；运行时各系统继续独立成目录，公共工具放在 `runtime/utils/`。自有头文件从层级名开始包含，例如 `runtime/resource/resource_system.hpp`、`editor/editor_system.hpp`、`plugins/editor/mcp/mcp_system.hpp`，搜索根由目标传递。`Engine` 只管理抽象根系统，不直接依赖窗口、渲染、脚本或项目模块。`RuntimeHost` 负责系统装配、窗口与帧调度，`GameplaySession` 负责玩法会话及场景资源清理；世界持有物理、导航、动画、字体、运行时 UI、音频及可选脚本子系统。引擎启动不需要项目，项目是编辑器宿主的启动上下文。

模块文档及独立类图按相同范围分层，统一评审入口与跨层引用关系保留在 `docs/class-diagrams/`。项目、资源管线和命令执行属于编辑器基础；资源构建、同步、建模编辑与 MCP 业务适配属于编辑器插件。资源依赖图与脚本执行属于运行时插件；纯几何建模与 MCP 协议属于编辑器插件。核心只声明最小能力和接口，宿主装配插件，核心不引用具体插件。插件清单与依赖边界见[插件说明](docs/plugins/plugins.md)。

各系统已接入相应依赖的头文件和实际库调用，Debug / Release 链接测试会实例化所有启用的系统。项目、编辑器和资源管线是独立宿主模块，运行时不依赖它们。已实现网格资源加载与直接光照 PBR；场景中的全部对象使用 Node + Component 表达，相机与灯光同样是节点组件。场景、网格、材质、Shader 和音频已支持资源文件导入与运行时二进制加载；Jolt 负责基础形状碰撞与刚体固定步模拟，SDL3 负责 PCM 音源播放、监听器空间衰减和生命周期。RmlUi 已支持场景 UI 文档绘制、鼠标键盘输入和控件事件；独立字体系统同时支持 UI 和 3D 曲线文字。完整编辑器界面尚未实现。桌面编辑器通过脚本插件接入 ES 模块（默认 QuickJS，可编译选择 V8）、项目玩法入口、默认写盘日志、完整资源版本同步和引擎热重载；使用方式见下方“桌面脚本与同步”。

Android 已提供 ARM64 原生构建和 APK 打包入口，平台资源集中在 `platforms/android/`，默认启用 QuickJS 脚本运行时，可用独立 V8 预设替换。APK 运行打包项目的 Gameplay，支持默认写盘日志、持久资源版本优先及包内资源回退；桌面 MCP 可通过 adb 同步到 Debug 包并控制引擎热重载。此前 V8 阶段的资源目录 v2、散文件与合并包已通过 Debug / Release APK 及 ARM64 模拟器验证，详见下方验证范围。项目启停工具用于桌面编辑器，Android 宿主直接启动打包 Gameplay。

仓库统一使用示例项目 `projects/showcase/`，入口菜单、基础方块、现代公寓、动画庭院和 3D 文字场景共用一套设置、资源目录和缓存。默认进入 UI 菜单；四个按钮通过项目 MVC 脚本打开对应场景，场景内的 `Back to scenes` 返回菜单，无需修改 `assets/scripts/game.ts`。入口继续导出 `modules/menu.js`，导航请求在 `Gameplay.Update()` 中处理。修改项目源脚本后，通过 MCP `stop_project → sync_assets → start_project` 更新缓存，或关闭后重新打开编辑器；仅停止再启动项目不会重新导入源脚本。TS 项目源码仍为 `.ts`，模块标识使用运行时 `.js` 后缀。TS 配置、package 与锁文件放 `settings/`，TS 源码和其他资源放 `assets/`，每个资源旁有唯一 GUID 的 `.meta`，生成 JS 与二进制资源统一放 `cache/gen/<完整 GUID>`，没有后缀。示例入口为 `assets/scripts/game.ts`；缓存产物及 `node_modules/` 不提交。

`engine_app` 使用默认引擎配置即可启动，不接受 `--project`。`editor_app --project <path>` 必须传入项目根目录；编辑器宿主先通过 `ProjectSystem` 检查根目录及 `settings/`、`cache/`、`assets/` 三个子目录，缺失或类型不是目录就返回失败，不创建窗口；检查通过后才准备脚本并启动引擎及借用项目的 `EditorSystem`。`ProjectSystem` 自身仍只负责目录检查，TS 设置由资源管线读取。资源描述通过 toml++ 无异常解析，失败用返回值报告。TaskSystem 使用 Taskflow 执行导入任务，`settings/import.toml` 的 `import_threads` 默认 4，可设为 1–64。

核心命名空间使用 `engine`，编辑器使用 `engine::editor`，插件使用 `engine::plugins::<插件名>`。核心不包含或链接编辑器、具体插件；编辑器基础库不包含或链接具体插件。插件实现下层声明的接口，由可执行宿主注入。建模 Mesh 导入扩展通过 `AssetPipelineSystem::set_importer` 登记；未启用建模插件时仍可导入普通 Mesh，权威建模源会明确报错。第三方类型尽量限制在实现中，FlatBuffers 唯一数据模型继续保留。

绑定、导入器、资源解码和组件编解码按类型独立文件，以最小接口和显式注册扩展。详细规则见 [AGENTS.md](AGENTS.md) 与[架构总览](docs/architecture.md)。CTest 的 `engine.architecture_boundaries` 检查源码包含方向及当前配置实际 `engine_*` 目标的链接方向、别名和循环；`engine.architecture_rules` 验证检查器的正反例。

## 字体与场景 UI

普通 `Node` 挂载 `UiComponent` 并引用 `.rml` 资源，一个文档内部可包含多个控件。`.rml.meta` 中的 `textures` 支持多贴图；每个 `[[ui.fonts]]` 绑定字体族别名、字体资源与独立 Shader，支持同时使用多字体、多 Shader。同一字体也可以通过不同别名采用不同 Shader；引用会进入资源依赖图及运行包。配置示例见 [UI 导入](docs/plugins/editor/ui-import.md)。

`font_import` 编辑器插件使用 FreeType 将原始 TTF 和 meta 参数转换为曲线与度量资源，`ui_import` 转换文档及引用。运行时核心 `FontSystem` 消费这些资源，UI 与普通节点上的 `TextComponent` 共用 GPU 字形缓存。默认 `builtin/shaders/font` 适配公开 bgfx / Slug 曲线覆盖率算法，项目 Shader 须符合[字体绘制契约](docs/runtime/font.md)。引擎不在运行时解析原始字体，也不按字号生成多份资源。示例使用 OFL 1.1 授权 Lato Regular / Bold，固定来源和许可随资源保存。

UI 脚本使用 MVC：项目普通对象作为 Model，`UiComponent` / `UiElement` 提供 View，派生 `UiController<T>` 处理事件和显式刷新。`Bind()` 执行 `OnBind → OnRefresh`，`Listen()` 自动登记订阅，`Unbind()` 释放订阅；文档、控件或脚本上下文销毁后旧引用失效。支持 click/change/input 及输入值快照，事件按目标到祖先冒泡，`target` 保留目标，`currentTarget` 表示当前订阅控件，业务无需逐帧轮询。控件支持纯文本、表单值、显隐、禁用、样式、类名及已登记纹理切换。API 与职责见 [MVC 类图](docs/plugins/runtime/class-diagrams/ui-script-api.md)，示例拆分在 `assets/scripts/models/` 与 `controllers/`。

当前 UI 支持内嵌 RCSS、基础控件几何、显式图片引用、矩形裁剪、变换、鼠标、滚轮、键盘和文本输入。字体支持导入字符范围内的 UTF-8、换行、legacy kerning 和缺字回退；尚未接入复杂文字整形、自动字体回退、RmlUi 高级 mask/filter/layer，以及脚本组件和项目场景跳转。外部样式、模板、脚本和未登记图片引用在导入时拒绝，避免运行包遗漏依赖。

字体/UI 基础接入阶段（V8 后端）已在 macOS Metal 完成 Debug / Release 构建，Debug 相关回归 9/9、Release 全套 CTest 74/74 通过；覆盖多字体、逐字体 Shader、多图片、多个文档同名字体别名隔离、鼠标捕获、输入焦点、3D 文字和停止后的 GPU 资源回收。正式编辑器经 MCP 验证待机、启动菜单、截图、停止恢复待机及有限帧退出。关闭 `font_import` / `ui_import` 后，编辑器与 MCP 仍构建、链接并启动成功；字体解析器仅由编辑器导入插件使用，RmlUi 的默认 FreeType 后端已关闭。 Android ARM64 Debug APK 已在独立 Vulkan 模拟器验证菜单、真实触摸设备事件、Tab 焦点、前后台恢复、120 帧正常退出、重启、MCP 完整资源同步及 3D 文字入口；APK 内不含 FreeType 和两个编辑器导入插件。该基础接入未验证 Android 实体设备、Windows DX12 或 iOS。

2026-10-30 的 UI MVC 初次绑定阶段验证在 macOS Metal 完成 Debug / Release 构建，两配置全套 CTest 均为 75/75。新增 `engine.ui_script` 使用真实 SDL 输入、V8 和 Metal，覆盖初始化绑定、Model/View 刷新、目标到祖先的冒泡顺序、订阅快照、解绑、文档/控件失效及脚本重载清理；异步事件回调会按脚本错误流程停止。字体/UI GPU 回归增加同 GUID 新旧资源并存与单个 View 替换释放检查。正式编辑器经 MCP 启动 MVC 菜单并截图、停止恢复待机，3600 帧正常退出。TypeScript 检查、154 个 Mermaid 块实际渲染和相关文档链接检查通过。记录保存在忽略的 `build/ui-mvc-*.log` 与 `build/ui-mvc-validation/`，截图在 `projects/showcase/cache/ui-mvc-validation/`；该次 UI MVC 绑定验证未重建或运行 Android APK。

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

推荐使用 `ninja-debug` / `ninja-release` Preset，构建窗口运行时（不包含脚本插件），三端统一使用 Ninja 并要求可运行的编译缓存工具。`debug` / `release` 使用平台默认生成器。按需启用编辑器、资源管线或脚本插件；`editor-deps` 同时启用运行时与资源管线。

```sh
git submodule update --init
cmake --preset runtime-deps
cmake --preset asset-pipeline-deps
cmake --preset editor-deps
```

这些配置会建立对应系统目标。编辑器 Debug / Release workflow 编译编辑器及基础运行时系统，运行生命周期、依赖链接、命令行，以及 TBB / USD 回归测试：

```sh
cmake --workflow --preset editor-deps
cmake --workflow --preset editor-deps-release
```

要把完整引擎的已选依赖及默认 QuickJS 脚本插件编译、链接并验证，使用：

```sh
cmake --workflow --preset ninja-engine-debug
cmake --workflow --preset ninja-engine-release
```

这两个 workflow 包括统一的 `engine_script_prepare` 步骤，并启用默认 QuickJS 桌面脚本运行；先按下节准备 Node.js、npm、Python 3 和示例项目的 TypeScript 依赖。已准备过且依赖未变时，可用 `cmake --preset ninja-engine-debug`、`cmake --build --preset ninja-engine-debug`、`ctest --preset ninja-engine-debug` 分步复跑。`engine-debug` / `engine-release` 提供相同系统组合，使用平台默认生成器。QuickJS 构建不准备、编译或链接 V8/ICU。显式 V8 构建也默认裁剪 Intl/ICU，因此默认产物均无 `icudtl.dat`。

依赖安装缓存位于 `build/dependencies/`，按 `runtime`、`editor`、`asset-pipeline`、`base` 四组 feature 隔离；Windows 静态预设另用 `build/dependencies/windows-static/`。同组 Debug / Release 共用安装目录，避免重复安装。vcpkg 自带二进制缓存继续生效，并保留用户设置的 `VCPKG_BINARY_SOURCES`。删除某个应用构建目录不会清掉这些共享依赖；只修改引擎源码时，无需运行清理依赖或重新准备依赖的命令。

CMake 通过 `CXX_COMPILER_LAUNCHER` 等目标属性接入缓存，覆盖引擎、自有测试与 ozz；Windows 优先查找 sccache，其他宿主优先查找 ccache，也可用 `ENGINE_COMPILER_CACHE_PROGRAM` 指定可执行文件。已有 `CMAKE_CXX_COMPILER_LAUNCHER` 设置优先保留。Windows 缓存目标使用 `/Z7` 将调试信息放入对象文件，避免共享编译 PDB 阻止命中。用 `ccache --show-stats` 或 `sccache --show-stats` 查看实际命中。

该机制适用于 Makefiles / Ninja，Visual Studio 生成器不支持此启动器属性；`ninja-*` 预设要求缓存工具存在且可执行，缺失时提示运行环境安装脚本。需要显式关闭时同时传入 `-DENGINE_REQUIRE_COMPILER_CACHE=OFF -DENGINE_USE_COMPILER_CACHE=OFF`。配置缓存 `CMakeCache.txt` 不等于对象文件缓存，Debug / Release 的不同编译选项仍分别缓存。对应机制见 [CMake 启动器文档](https://cmake.org/cmake/help/latest/prop_tgt/LANG_COMPILER_LAUNCHER.html)和 [sccache 的 CMake / MSVC 说明](https://github.com/mozilla/sccache#usage)。

ozz 的源码副本仅在固定提交、覆盖文件或准备脚本变化，以及副本缺失时更新；内容相同的文件保留时间戳，无变化构建不重复运行准备。仅选择 V8 时使用独立 GN 工具链及成功构建缓存，不经过 CMake 的编译启动器：校验固定源码、补丁、平台 / SDK / 工具链和 GN 参数后，Debug / Release 分别复用已验证的库和头文件；命中时跳过同步、GN、Ninja 与归档合并。缺失产物或输入变化会使缓存失效，失败的构建不会留下成功标记。V8 自带的固定版本 Ninja 保持独立，不替换为环境安装的版本。

第三方本地补丁放在 `third_party/`，构建前的覆盖流程见[开发与第三方依赖](docs/development.md)。

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

Debug 使用对应的 `windows-static-debug` 和 `windows-static-debug-prepare`，编辑器仍通过 `--project` 指定源项目。上例的 `engine_project_export` 将运行程序导出到 `output/windows/x64/release/runtime/`，其中包含 `engine-showcase.exe`、`catalog.bin`，以及目录引用的 `packs/` 或 `gen/` 资源文件。默认构建不部署 ICU 数据。EXE 所在目录是 App 的 builtin 资源根，资源保持独立文件，随整个目录一起分发。

V8 在 Windows、macOS、Linux、Android 等平台统一默认关闭 WebAssembly、Intl/ICU、Temporal 和系统 instrumentation（包括 Windows ETW），保留 Sparkplug、Maglev、TurboFan 等 JIT、指针压缩、sandbox 与既有编译优化等级。对应 CMake 开关为 `ENGINE_V8_ENABLE_WEBASSEMBLY`、`ENGINE_V8_ENABLE_I18N`、`ENGINE_V8_ENABLE_TEMPORAL`、`ENGINE_V8_ENABLE_SYSTEM_INSTRUMENTATION`，全部默认 OFF。项目脚本不能使用已关闭的全局 API；默认没有 ICU 资源部署。裁剪参数参与缓存身份，精简归档与头文件存放在带 `-f0000` 的独立目录；此前完整配置的缓存保留。

Windows 修复分支此前的验证记录（2026-10-30，本轮未重新执行 Windows）：x64 静态组合的 Debug / Release 全量 CTest 均为 75/75，使用真实 DX12。静态编辑器和项目程序的 PE 导入仅包含 Windows 系统 DLL；两者分别复制到中文/emoji 路径、仅保留一个 EXE 及独立数据，在 PATH 仅含 Windows/System32 时均完成 120 帧并正常退出。正式 showcase 导出 1134 项资源、一个独立合并包；同一编辑器进程的实际运行日志记录四个菜单场景均成功打开并正常退出。验证日志位于忽略的 `build/windows-static-*-test.log` 与 `build/windows-validation/static-final/`。

依赖维护需保留以下兼容约束：

- `basisu` overlay 补充 macOS ARM64 与 Android 支持，Android 保留编码 / 转码库并关闭宿主工具；上游 port 提供等效支持后才能移除覆盖。
- Android SDL3 静态合入 `libmain.so` 并安装同版本 Java 入口，bgfx overlay 传递所需 Android 系统库，避免 Java / JNI 或链接依赖不一致。
- bgfx Metal overlay 在绑定 uniform 前按需分段预留空间，并检查每次写入边界；旧段等待 GPU 完成后释放，避免大型场景超过固定 8 MiB 缓冲时越界。macOS / iOS 使用该补丁；上游提供等效修复后再移除，详见 `third_party/overlay-ports/README.md`。
- macOS TBB 使用共享库，防止 USD 动态库重复嵌入独立调度器；任务内存释放补丁回补 oneTBB 提交 `132268ddd04d74e8b2e21e431a7e770761def49f`。升级包含该修复的版本后可移除补丁，共享链接策略须单独保留或验证替代方案。
- USD 插件资源在 Windows 动态依赖中位于对应配置的 `bin/usd/*/resources/`，静态依赖及 macOS 位于 `lib/usd/*/resources/`。Windows 静态编辑器将注册信息和 schema 数据部署到 EXE 旁的 `usd/`，数据继续外置；现有 USD 测试按配置使用 SDK 或本地部署的数据。TBB / USD 回归同时检查数据保存、加载和正常退出。

### 交付目录

编译目录 `build/` 保留增量缓存、静态库和调试符号；最终交付物放在仓库根 `output/<platform>/<arch>/<debug|release>/`，其中 `editor/` 为桌面编辑器，`runtime/` 为项目运行包。`output/` 不提交 Git。依赖尽量静态合入程序，这与“把所有依赖改成动态库”不同。

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

原生桌面模式的 `catalog.bin`、`packs/` 或 `gen/` 位于程序旁；默认不包含 ICU 数据。可选 `.app` 模式将它们放在 `Contents/Resources/`，由 SDL 直接读取；宿主优先使用已发布的外部持久资源版本，否则使用包内版本，缺失条目不混用旧包。运行时直接读取资源，不使用自解压程序。编辑器继续要求 `--project`；运行包直接进入 Gameplay。若需要 macOS 原生应用包，配置时显式加 `-DENGINE_MACOS_APP_BUNDLE=ON`，再运行相同导出目标；程序、私有工具和初始资源均进入 `.app`，外部持久目录用于更新资源版本。本地构建仅使用系统要求的 ad-hoc 代码签名，不要求证书或用户确认；正式分发的开发者签名和公证另由发布流程提供。Linux / iOS 暂未提供或验证上述导出入口。

编辑器导出包含 Shader 编译器、Node.js、引擎声明、Schema 和固定版本的 TypeScript。导入优先使用项目安装的 TypeScript；项目未安装时，只有声明版本与随包版本一致才使用随包编译器。部署目录存在但工具缺失会报告失败，避免意外借用构建电脑的工具。资源运行包不包含这些编辑器工具。

macOS 编辑器的 `lib/` 同时包含 USD 导入所需的 `libusd_*` / TBB，以及随包 Node.js 工具的依赖。当前 Node.js 使用 `libnode`、`libicu*`、SSL 等动态库；这些属于离线 TypeScript 编译工具，不是引擎脚本 VM 的依赖，不受 `ENGINE_SCRIPT_BACKEND` 切换影响，也不进入项目运行包。引擎默认不部署的 `icudtl.dat` 与 Node.js 的 ICU 动态库需要区分。

跨平台交付阶段的 Mac 复核（提交 `57f8bf6`，当时使用 V8）完成 Debug / Release CTest 各 78/78。原生运行包搬迁后以受限 PATH 完成 Metal 菜单及 120 帧退出；编辑器搬迁后，在无项目 node_modules 的独立项目中实际导入 TypeScript、Shader 和 USD，再运行 120 帧退出。可选 `.app` 内置资源直接读取及 120 帧退出、原生与应用包的 USD 注册和私有动态依赖闭包均通过。该阶段仅审查 Windows 代码及跨平台构建契约，未在 Windows 设备重跑。

同一跨平台交付阶段的 V8 Android Debug / Release APK 均通过单 `libmain.so`、系统动态依赖、签名、ZIP / ELF 16 KiB 对齐及 1134 项资源完整性检查。在独立 API 34 ARM64 软件 Vulkan 模拟器上，两配置已验证菜单、四场景进入并返回、键盘焦点、前后台同进程恢复、120 帧正常退出和 V8 执行。最终 Release 包重新完成上述全流程；最终 Debug 包在此前全流程基础上复核安装哈希、菜单和有限帧退出。Android 预设显式装配角色控制器，避免旧 CMake 缓存遗漏插件导致庭院加载失败。日志、截图及安装哈希回执保存在忽略的 `build/windows-cross-platform-review/`；未验证 Android 实体设备或 16 KiB 页设备。

### 插件选择

插件由 CMake 清单选择，不使用额外 JSON 清单。每个插件在 `cmake/plugins/<runtime|editor>/<name>.cmake` 声明自己的目标、源码和依赖；注册器解析依赖闭包后才创建目标，宿主按实际目标装配。运行时插件包括 `resource_graph`、`script` 与可选角色输入/相机控制器；建模（包括纯几何）、MCP（包括协议）、资源构建、同步和 FBX/USD 格式导入均属于编辑器插件。

例如，使用已准备好的依赖配置纯运行时窗口且不启用资源依赖图：

```sh
cmake --preset ninja-debug -DENGINE_RUNTIME_PLUGINS= -DENGINE_EDITOR_PLUGINS=
cmake --build --preset ninja-debug
ctest --preset ninja-debug
```

如需恢复资源依赖图，在配置时传入 `-DENGINE_RUNTIME_PLUGINS=resource_graph`；完整脚本运行使用 `'-DENGINE_RUNTIME_PLUGINS=resource_graph;script'`，默认选择 QuickJS，显式传入 `-DENGINE_SCRIPT_BACKEND=v8` 才选择 V8。依赖闭包会加入已选插件所需的其他插件；要排除某插件，必须同时移除依赖它的入口。完整能力清单见[插件说明](docs/plugins/plugins.md)。核心 `ResourceSystem` 始终可单项加载，完整依赖加载与共享释放由选中的 `ResourceGraphSystem` 提供。

脚本插件位于 `plugins/runtime/script/`，包含 `ScriptSystem`、编译期选定的虚拟机执行与共享逐类绑定。核心只保留 `ScriptRuntime` / `ScriptStatus` 契约，`RuntimeHost` 通过宿主提供的可空工厂装配；关闭插件不链接脚本虚拟机，仍可运行窗口、渲染、编辑器及资源导入，依赖脚本的项目启动会明确返回失败。当前宿主的 `save_scene` 命令需同时启用资源管线与建模插件；关闭建模插件后仍支持普通 Mesh 导入。

MCP 在电脑端经 adb 同步 Android 调试包，设备端不运行 MCP；正式游戏不链接建模、MCP 或编辑器构建/同步工具。仅开启运行时和脚本不会默认启用资源同步插件；默认同步插件还要求编辑器或资源管线能力。未来设备调试插件必须按构建用途区分正式运行与调试运行，不能仅靠程序中的运行开关隐藏调试接口。这些开关是构建期选择，不表示支持 C++ 插件热插拔。

CMake 的其余职责分别位于 `install/`、`dependencies/`、`core/`、`editor/`、`platforms/`。新增插件在自己的声明文件维护源码，不向核心中央清单追加业务文件。

### 脚本后端选择

`ENGINE_SCRIPT_BACKEND` 只能设为 `quickjs` 或 `v8`，默认 `quickjs`。这是编译期互斥选择：脚本目标只定义 `ENGINE_SCRIPT_BACKEND_QUICKJS` 或 `ENGINE_SCRIPT_BACKEND_V8`，只加入对应适配源码和库，不提供运行时切换，不把两套虚拟机装进同一产物。具体 VM 的头文件和句柄限于脚本插件内部，逐类绑定通过同一个 [ScriptVm](docs/plugins/runtime/class-diagrams/script-vm.md) 接口工作；Gameplay 生命周期、UI MVC、资源快照及热重载共用实现。

| 用途 | 默认 QuickJS 预设 | 显式 V8 预设 |
| --- | --- | --- |
| 完整桌面引擎 | `ninja-engine-debug/release` | `ninja-engine-v8-debug/release` |
| macOS 原生交付 | `macos-native-debug/release` | `macos-native-v8-debug/release` |
| Windows 静态交付 | `windows-static-debug/release` | `windows-static-v8-debug/release` |
| Android ARM64 | `android-arm64-debug/release` | `android-arm64-v8-debug/release` |

表中的 `debug/release` 表示两个独立预设。每个脚本宿主构建的 `-prepare` 预设统一调用 `engine_script_prepare`：QuickJS 由所选 `quickjs` vcpkg feature 在配置阶段安装，准备目标无需额外工作；V8 才触发 GN 准备。QuickJS 不查找、准备或链接 V8/ICU，V8 不加入 QuickJS feature。未选择 `runtime/script` 时不因默认后端值引入 VM。建议使用独立预设保留两种构建缓存；切换现有配置时须重新 configure，不能只改 C++ 宏。

例如选择 V8 完整桌面构建：

```sh
cmake --preset ninja-engine-v8-debug
cmake --build --preset ninja-engine-v8-debug-prepare
cmake --build --preset ninja-engine-v8-debug
ctest --preset ninja-engine-v8-debug
```

QuickJS 使用固定的 quickjs-ng 0.10.1 vcpkg port；其编译期模块图检查扩展由 `third_party/overlay-ports/quickjs-ng/` 的补丁维护，不修改上游安装目录。首次安装按 vcpkg 二进制缓存复用，日常代码修改不重新下载或准备依赖。两个后端均只运行项目 JavaScript，不加载 Node.js；模块预检不执行顶层代码，活动与候选 Run 的 VM、模块和 Promise 队列独立。顶层 await 与动态 import 不在当前同步契约内。

本轮 QuickJS 验证（2026-10-30）：Mac Release 全量 CTest 77/77；Debug 77 项均已通过，其中 MCP 传输测试在并行重编和模拟器负载下两次超过原有 60 秒限制，停止重编后单独复跑在 47.75 秒通过，未放宽超时。最后的模块路径补丁另在两配置通过脚本、UI、动画、项目生命周期、插件选择和架构边界回归。覆盖循环模块、缺失导出、顶层 await 预检、拒绝动态 import、Promise 清理、BigInt 身份及 NUL 路径。默认原生运行包和编辑器搬迁到中文路径、限制 PATH 后均运行 120 帧正常退出，主程序符号确认只有 QuickJS，无 V8 或 ICU 数据。

Mac 裁剪 V8 Debug / Release 均完成真实宿主链接，并通过 `engine.v8`、`engine.script_runtime`、`engine.ui_script`、`engine.model_animation_script` 四项定向回归；独立上游验证还覆盖 V8 缓存和插件选择。两配置运行包均从搬迁目录直接读取独立资源，启动菜单后运行 120 帧正常退出；未链接 QuickJS，未生成或部署 ICU 数据。Release 同项目主程序大小为 QuickJS 12,059,888 字节、V8 45,769,024 字节，独立资源另计。这些是本次构建结果，不是性能测试。

Android QuickJS Debug / Release 最终 APK 均验证唯一 `libmain.so`、系统动态依赖、签名、ZIP / ELF 16 KiB 对齐和 1134 项运行资源。最终 Release 在独立 API 34 ARM64 软件 Vulkan 模拟器完成四场景进入/返回、键盘焦点、同进程前后台恢复和 120 帧正常退出；Debug 先完成全流程，更新模块路径补丁后的最终包再次核对安装哈希、菜单及 120 帧退出。APK 分别为 51,341,503 / 31,951,764 字节，不包含 V8/ICU；这些大小包含相同项目资源，不代表 VM 库本身体积。证据保留在忽略的 `build/quickjs-validation/`。本轮未在 Windows、Linux、iOS、Android 实体机或 16 KiB 页设备重新运行；统一配置不等于这些设备已经验证。

按用户要求，后续验证已停止。裁剪 V8 的 Android 配置已生成，但首次依赖编译未完成，本轮没有该组合的 APK 运行结果；已完成的 QuickJS Android 和两种 VM 的 Mac 结果不受影响。

### V8 依赖

只有显式选择 V8 后端或启用独立 V8 验证才准备 V8。`v8-debug/release` 用于单独验证上游嵌入库，不装配脚本插件，因此其 `v8-prepare-debug/release` 保留专用 `engine_v8_prepare` 目标。初始化子模块、设置 `VCPKG_ROOT` 后，在原生 x64 或 macOS ARM64 平台显式执行：

```sh
cmake --preset v8-release
cmake --build --preset v8-prepare-release
cmake --build --preset v8-release
ctest --preset v8-release
cmake --preset v8-debug
cmake --build --preset v8-prepare-debug
cmake --build --preset v8-debug
ctest --preset v8-debug
```

也可以使用 `cmake --workflow --preset v8-release` 或 `v8-debug` 串联上述准备、构建与测试。首次准备会联网获取 V8 `DEPS` 固定的 depot_tools、GN、Ninja、Clang 及其他构建依赖；编译需要较多磁盘、内存和时间。默认并行度为 4，可在配置时用 `-DENGINE_V8_JOBS=2` 调低。网络环境需要代理时，在运行前设置 `HTTP_PROXY` / `HTTPS_PROXY`。

Windows 使用本机 VS 2022 C++ 工具链和 Windows SDK 10.0.26100.0，SDK 需包含 Debugging Tools for Windows。准备过程不升级系统工具链；按需通过已有 SDK 安装器添加缺少的组件。

macOS 使用本机 Xcode SDK 与链接器，Clang、Rust 仍由固定 V8 `DEPS` 提供。与 Windows 相同，嵌入库保留宿主的 `malloc/new`，关闭全局 allocator shim。为兼容系统 SDK，GN 关闭显式 Clang 模块并使用 Xcode 链接器；当下载的 Clang 包缺少 `libLTO.dylib` 时，构建副本中的兼容补丁通过 `xcrun` 选择现有 Xcode 库，并让 Rust 编译期宏使用同一链接包装器。另为 SDK 27 的 C++20 数学头补充缺失的 `INFINITY` / `NAN` 宏，并在编译器不支持 `stack_protector_ignore` 时，仅在 V8 的 signpost 调用处忽略该属性诊断。

下载、源码副本、Git 配置和编译产物均位于忽略的 `build/v8/`，不会修改 `third_party/v8/` 或用户全局 Git 配置。Windows 源码副本应用仓库保存的兼容补丁，按 `CMAKE_MSVC_RUNTIME_LIBRARY` 匹配 `/MDd` / `/MD` 或 `/MTd` / `/MT`；静态 CRT 使用独立的 `out/engine.mt-debug`、`out/engine.mt-release` 和头文件缓存，不覆盖动态 CRT 产物。两种配置均关闭全局 allocator shim，保留宿主 CRT 分配器。桌面构建同时运行上游嵌入样例；`engine.v8` CTest 通过 CMake 的 `engine::v8` 目标执行 JavaScript，验证共享内存、按配置启用的 Intl/Temporal/WebAssembly，或确认关闭的能力确实不存在。Android 交叉编译只构建上游样例，执行验证放到目标设备上的 APK 测试中，不在宿主运行 Android 程序。启用国际化时，将 `ENGINE_V8_ICU_DATA` 指向的 `icudtl.dat` 复制到程序旁，测试目标已自动部署；关闭时不需要 ICU 数据。使用 `symbol_level=0` 控制产物体积，Debug 保留 V8 断言但不生成完整调试符号。

GN 的 monolith 未收集隐式 Rust 归档。`cmake/dependencies/package_v8.cmake` 按 GN 实际依赖清单额外合并 Rust 与 Clang 运行库，生成 `engine_v8.lib`（Windows）或 `libengine_v8.a`；macOS 先从 Clang 通用运行库提取本机架构的副本，再合并归档。`engine::v8` 自动使用这个完整嵌入库，并在 macOS 传递 Foundation、CoreFoundation 和 Security 框架依赖。关闭 Temporal 时允许 GN 不生成 Rust 归档；启用时仍校验完整依赖。原 GN 库和上游样例保留。

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
./build/ninja-engine-debug/editor_app --project projects/showcase
```

该入口验证项目并建立引擎、ImGui 上下文；完整引擎预设还会构建并同步项目脚本，等待 MCP `start_project` 后运行。尚未绘制完整编辑器界面。

普通 CTest 不要求桌面环境。要在有图形桌面的机器运行真实窗口/GPU测试：

```sh
cmake --preset ninja-debug -DENGINE_ENABLE_GPU_TESTS=ON
cmake --build --preset ninja-debug
ctest --preset ninja-debug
```

`engine.font_ui_render` 验证多字体、多 Shader、多图片、跨文档输入焦点、3D 文字、缩放与反复启停后的 GPU 资源回收。`engine.window_render` 验证真实后端、窗口生命周期、方块提交、场景切换及重复启停后实际 bgfx 资源回到待机基线。`editor.project_lifecycle` 使用真实编辑器、TypeScript 编译器和 MCP，验证空白待机、启停、重载和失败清理。`editor.mcp_commands` 验证场景查询、组件修改、批量回滚、撤销与会话隔离。这些窗口测试仅在 `ENGINE_ENABLE_GPU_TESTS=ON` 时注册并串行执行；命令执行、协议注册和文件传输测试无需 GPU。

### 桌面脚本与同步

桌面编辑器与 Android 共用运行时脚本和热重载实现。宿主需有可执行的 Node.js、npm 和 Python 3；Node.js 仅用于编译工具，不进入玩法虚拟机。先安装项目锁定的 TypeScript，再构建完整引擎预设：

```sh
npm ci --prefix projects/showcase/settings
cmake --workflow --preset ninja-engine-debug
cmake --workflow --preset ninja-engine-release
```

从仓库根目录启动编辑器，指定一个持久运行目录，让编辑器和 MCP 工具访问同一份缓存与状态：

```sh
./build/ninja-engine-debug/editor_app --project projects/showcase --runtime-dir build/editor-runtime
```

Windows 将可执行文件写为 `build/ninja-engine-debug/editor_app.exe`；Release 对应 `build/ninja-engine-release/`。可加 `--frames 120` 做有限帧运行。不传 `--runtime-dir` 时使用按项目区分的系统应用数据目录，启动日志会打印实际路径。

编辑器先导入 `assets/`：读取 `.meta` 身份、解析资源描述、编译 TS 与目标平台 Shader，并生成 `cache/gen/<完整 GUID>`。SQLite 在 `cache/assets.sqlite` 记录输入哈希、依赖、工具版本与生成版本；再次导入只转换变化及受影响资源，失败保留上一版完整输出。随后建立本会话不变的内置资源快照并发布持久资源版本，全部引擎系统就绪后保持空白，等待 MCP 的 `start_project`。启动时示例 Gameplay 调用 `Scene.Load("project/scenes/launcher.scene")`，显示可进入四个场景的菜单，停止时释放场景及本次运行 GPU 资源。

编辑器运行时，若在另一终端修改了项目源脚本或其他资源，可执行：

```sh
python3 scripts/mcp_client.py --server build/ninja-engine-debug/mcp_app --project projects/showcase --runtime-dir build/editor-runtime status
python3 scripts/mcp_client.py --server build/ninja-engine-debug/mcp_app --project projects/showcase --runtime-dir build/editor-runtime start
python3 scripts/mcp_client.py --server build/ninja-engine-debug/mcp_app --project projects/showcase --runtime-dir build/editor-runtime sync
python3 scripts/mcp_client.py --server build/ninja-engine-debug/mcp_app --project projects/showcase --runtime-dir build/editor-runtime hot-reload on
python3 scripts/mcp_client.py --server build/ninja-engine-debug/mcp_app --project projects/showcase --runtime-dir build/editor-runtime status
python3 scripts/mcp_client.py --server build/ninja-engine-debug/mcp_app --project projects/showcase --runtime-dir build/editor-runtime hot-reload off
python3 scripts/mcp_client.py --server build/ninja-engine-debug/mcp_app --project projects/showcase --runtime-dir build/editor-runtime stop
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

每次玩法运行只有一个项目入口，可直接承载业务；它独立于节点。`OnInitialize` 必须返回布尔值。`Debug.Log`、`Debug.LogWarning`、`Debug.LogError` 接受字符串并映射到原生日志。`Scene.Load(path)` 从当前目录按需加载场景及必要依赖，`Scene.FindNode(name)` 查找节点；`Scene.Create` 可以动态创建所属玩法会话的场景；`Scene.CreateNode` 创建节点，节点可以添加 `Camera`、`Light`、`MeshRenderer`、`Collider`、`RigidBody`、`AudioSource`、`AudioListener` 组件。物理与音频接口见[脚本类图](docs/plugins/runtime/class-diagrams/physics-audio-script-api.md)。全部场景对象都遵守这个结构。节点管理父子关系，Transform 提供局部位置、旋转与缩放；`Time.deltaTime` 为当前 Update 的秒数。完整用法见 [示例 Game](projects/showcase/assets/scripts/game.ts) 与 [SDK 声明](include/engine.d.ts)。停止或重载后旧句柄失效，失败返回 false/null。

`App` 的静态只读路径具有以下含义：

| 脚本接口                 | 用途                                                                      |
| ------------------------ | ------------------------------------------------------------------------- |
| `App.builtinAssetsPath`  | 内置资源根目录；编辑器中映射项目转换缓存，真机为包内资源                  |
| `App.persistentDataPath` | 持久数据根目录，保存同步版本、日志、控制与状态；可由 `--runtime-dir` 指定 |
| `App.cachePath`          | 平台临时缓存目录，可重建；不承载需要跨启动保留的同步版本                  |

`LogSystem` 默认将日志写入 `persistentDataPath/logs/engine.log` 并通过 SDL 输出到平台日志（桌面 stderr、Android logcat）；单文件达到 4 MiB 时轮转，保留 3 个旧文件。项目编译输出 `cache/gen/` 与平台临时 `App.cachePath` 是不同目录。

持久版本保存在 `persistentDataPath/assets/versions/<revision>/`，只含 `catalog.bin` 和其引用的散文件或合并包，SQLite 不进包。全部内容核对大小及 FNV-1a 64 哈希后，才原子发布 `assets/available.json`。运行开始先选择完整持久版本，未发布时使用内置版本；运行模块和场景持有实际加载资源的共享引用；核心目录只保存元数据，不预读所有实体。TS 相对 import 通过清单中的模块路径映射到 GUID，已删除模块不会回退到旧包体。绝对路径、越界路径和越界符号链接会被拒绝。

文件读取统一通过 SDL3 同步接口，单实体上限 64 MiB；资源目录最多 65536 项，合并包按成员范围读取，不要求整包进入内存；脚本源码及部署控制 JSON 文本另有 4 MiB 上限，单次模块图最多 256 个模块。运行时支持相对路径静态 ES import 和 `engine` 模块，不提供动态 import、顶层 await 或 Node.js API，也不自动打包任意 npm 运行时依赖。TS 的相对 import 应使用生成后的 `.js` 路径。

MCP 同步整个资源目录，包括场景、Mesh、材质、Shader、脚本及其他普通资源；按 GUID 将完整版本发布到本机或 Android Debug 包的持久目录，通过 `script-control.json` / `script-status.json` 控制引擎；不提供独立网络服务或差量同步。`ENGINE_ENABLE_SCRIPT_RUNTIME` 在完整桌面和 Android 预设均开启；它决定 `runtime/script` 的默认选择，显式插件清单优先；关闭插件后核心和宿主不链接脚本虚拟机。单独开启 V8 依赖时由独立测试验证上游库，不创建伪脚本系统。Android 传输使用 adb，具体命令如下。

### 渲染与项目控制

- 同一编辑器窗口在空白与项目画面间整体切换。场景、节点和组件归当前运行；共享窗口与图形设备保留到编辑器关闭。停止会清除最后一帧，重复启动不会创建第二套实例。
- 示例使用 `.mesh` 文件中的 24 顶点、36 索引方块，运行时按导入网格创建 GPU 缓冲；支持无光照材质与基础色/金属度/粗糙度的 PBR。光照采用 GGX、Smith、Schlick；最多 64 盏方向/点/聚光灯，视角来自活动透视或正交 Camera 节点；基础色、法线及 ORM 贴图由 BasisU 缓存提供。支持场景中唯一启用投影的方向光 1/2/4 级 CSM、环境光、IBL、屏幕空间 AO、可选线性 HDR 场景合成与 Bloom、曝光与 ACES 色调映射；最终输出为 SDR。尚未实现点光/聚光阴影或透明混合。
- 内置 Shader 使用宿主 shaderc 在构建时编译为数组，项目 `.shader` 描述引用独立源码 GUID，由导入器编译为目标平台二进制；runtime feature 也声明宿主 bgfx[tools] 依赖。目标后端固定为 Metal、DirectX 12 或 Vulkan，不依赖运行目录中的示例 Shader 文件。可用 `ENGINE_SHADERC_EXECUTABLE` 指定宿主工具，缺失或无法执行时配置失败。
- 桌面 MCP 提供无参数 `start_project`、`stop_project`（客户端简写 `start`、`stop`）。目标固定为当前 `--project` 和 `--runtime-dir`，须先启动编辑器。Android 工具提供同步、热重载和状态查询；Android 由平台宿主启动 Gameplay，不提供编辑器项目启停。
- `project-control.json` / `project-status.json` 位于持久运行目录。请求带会话与编号，由编辑器主线程执行；状态提供 `projectState`、`session`、`lastRequestId`、`lastRequestSucceeded`。场景和 GPU 数量查询分别属于场景与渲染系统，不从 Engine 转发。
- 启停等待最多 10 秒；超时的 `activation: pending` 表示尚未确认，**不是取消**。查询同一请求的最终状态后再操作。旧编辑器会话请求不会在新会话重放。
- 一个运行目录供一个编辑器实例使用；并行编辑器使用不同 `--runtime-dir`。MCP 通过 `project-control.lock` 串行化控制；若 MCP 被强制中止留下锁，先确认相关客户端已退出且状态中没有正在执行的请求，再移除该运行目录中的锁目录。不能删除其他活动操作的锁。

### 帧率上限

核心 `Engine` 提供整帧限速，运行时默认 `-1` 表示不限帧。正整数为每秒帧数上限，`0`、小于 `-1` 的值或脚本非整数输入被拒绝，原设置保持不变。宿主在完整帧开始和结束调用限帧接口，休眠只补足剩余帧预算，超预算不额外等待；下一帧真实时间差包含等待时间，物理固定步长不变。

项目编辑器配置放 `settings/editor.toml`，文件或字段缺失时默认 60，格式或数值错误拒绝启动：

```toml
[editor]
target_frame_rate = 60
```

该上限用于编辑器待机和场景预览。启动游戏前暂存当前上限并切到 `-1`，再执行项目入口；业务可通过脚本设置自己的上限。停止游戏、启动失败或运行异常停止后恢复暂存值，业务修改不写回编辑器配置。独立运行包不读取编辑器配置。

脚本在 `OnInitialize`、`Update` 或组件生命周期回调中使用：

```ts
import { Engine, Gameplay } from "engine";

export default class Game extends Gameplay {
    override OnInitialize(): boolean {
        return Engine.SetTargetFrameRate(45);
    }
}
```

模块顶层、构造与热重载候选预检阶段禁止修改上限；查询在这些阶段返回 `null`。限帧是上限而非保底帧率，设备负载和 VSync 仍可能使实际帧率更低。C++ 接口与所有权见[帧率限制](docs/runtime/class-diagrams/frame-limiter.md)。

### 编辑器场景预览

编辑器可直接预览已导入的 `.scene` 资源，不启动 Gameplay、角色控制、物理模拟或音频播放；窗口显示场景，不添加编辑器 UI。调用 `open_scene_preview` 后，既有 MCP 节点、组件和材质命令直接编辑该预览，下一帧显示结果。

| 工具 | 参数与作用 |
| --- | --- |
| `open_scene_preview` | `path` 为完整逻辑资源路径；可选 `camera` 对象含 `position`、`rotation`、`speed`；替换当前场景并丢弃未保存内存编辑 |
| `get_scene_preview` | 无参数；查询 active、path、sceneId、camera、modified、reloadPending、lastError |
| `set_preview_camera` | 至少一项 position、rotation、speed；修改独立镜头，不改变场景内容 |
| `refresh_scene_preview` | 可选 `discardChanges`，默认 false；先增量导入源资源再重建预览，丢弃未保存编辑须显式传 true |
| `close_scene_preview` | 无参数；丢弃未保存内存编辑，释放预览场景和 GPU 资源，恢复空白窗口 |

镜头默认复制场景活动相机的世界姿态和投影；无活动相机时使用位置 `[0,2,5]`、旋转 `[0,0,0]`。它由编辑器独立持有，不进入项目场景节点、`get_scene` 或 `save_scene`。旋转为 `[pitch,yaw,roll]` 度，移动速度为 0.01..1000 米/秒，默认 5；按住鼠标右键后转动视角，WASD 移动，Q/E 世界竖直升降，Shift 四倍加速。移动镜头不标记场景已编辑。环境光、雾、清屏色从场景环境组件读取，曝光、色调映射、AO、HDR 与 Bloom 从场景活动相机的后处理组件读取；隐藏预览镜头只改变观察视角，不覆盖这些参数。无需先执行 Gameplay，预览和运行使用同一份场景设置。

源资源或项目设置变化后，编辑器监听并增量导入；外部变化在无未保存编辑时自动刷新预览。有未保存的 MCP 编辑时保留当前画面并标记 `modified`、`reloadPending`，调用方可明确 `refresh_scene_preview` 并传 `discardChanges=true` 接受外部版本。`save_scene` 保存到当前源路径后，预览校验本次源文件、meta 摘要及目录变化，仅由自身保存引起的导入直接更新资源目录，保留当前场景、镜头和撤销/重做历史；保存后继续编辑也不会被这次导入清除。

同一场景代次内，已成功保存的场景及导出资源若被外部修改或丢失 meta，再次保存会报告冲突并保留外部文件；需刷新接受新版本，或恢复原文件后再保存。显式打开其他场景或关闭预览会丢弃未保存内存编辑，不会自动保存；回执的 `discardedChanges` 说明是否丢弃了修改。刷新、关闭和切换场景后，继续编辑前重新查询 sceneId/nodeId。

```sh
python3 scripts/mcp_client.py --server build/ninja-engine-debug/mcp_app --project projects/showcase --runtime-dir build/editor-runtime call open_scene_preview --arguments '{"path":"project/scenes/apartment.scene"}'
python3 scripts/mcp_client.py --server build/ninja-engine-debug/mcp_app --project projects/showcase --runtime-dir build/editor-runtime call set_preview_camera --arguments '{"position":[0,2,5],"rotation":[0,0,0],"speed":5}'
python3 scripts/mcp_client.py --server build/ninja-engine-debug/mcp_app --project projects/showcase --runtime-dir build/editor-runtime call get_scene_preview
python3 scripts/mcp_client.py --server build/ninja-engine-debug/mcp_app --project projects/showcase --runtime-dir build/editor-runtime call close_scene_preview
```

这些工具只在桌面编辑器目标注册，沿用主线程命令通道及原执行回执；超时仍按 pending 查询，MCP 不直接创建场景、驱动镜头或执行导入。类图见[场景预览 MCP](docs/plugins/editor/class-diagrams/scene-preview-mcp-tools.md)。

效果调试使用 `scripts/preview_visual_review.py`，按项目 `settings/visual-review.json` 的固定路线移动隐藏预览镜头、等待真实截图完成，并保存机位、渲染参数、源文件哈希和回执。`--compare` 可生成相同路线的前后滑动对比单文件报告，产物位于项目 `cache/visual-reviews/`。工具仅使用 Python 3.11+ 标准库，不启停编辑器，也不修改场景；路线格式、运行命令和分析顺序见[场景效果巡览](docs/editor/visual-review.md)。

### MCP 模块注册与场景编辑

`McpSystem` 只处理协议、工具注册和参数检查。`ProjectMcpTools` 登记原有项目启停、资源构建、同步与状态工具；`EditorMcpTools` 登记桌面场景工具。各模块提供工具名称、参数模型和回调，宿主在 MCP 初始化前登记。新增模块不需要改协议分派函数；需要主线程操作的工具回调可复用 `EditorMcpTools::call_command`，由对应编辑器模块注册和执行命令。

| 工具 | 参数与作用 |
| --- | --- |
| `get_scene` | 可选 `offset`、`limit` 分页查询活动场景、节点和组件，待机返回 `scene: null` |
| `create_node` | `sceneId`、`name`、`component`（`empty` / `camera` / `light`）；创建带 Transform 的唯一命名节点 |
| `set_active_camera` | `sceneId`、`nodeId`；选择场景中的 Camera 节点 |
| `set_transform` | `sceneId`、`nodeId`，以及至少一项 `position` / `rotation` / `scale` 三分量数组 |
| `set_camera` | `sceneId`、`nodeId`；设置投影、视角、裁剪面或正交尺寸 |
| `set_light` | `sceneId`、`nodeId`；设置颜色、强度、类型、范围或聚光半角 |
| `set_material_type` | `sceneId`、`nodeId`、`path`、可选 `slot`；按完整路径选择材质资源 |
| `set_material_parameter` | `sceneId`、`nodeId`、`name`、`type`、`value`、可选 `slot`；类型为 `float` / `v2` / `v3` / `v4` |
| `set_material_texture` | `sceneId`、`nodeId`、`name`、`path`、可选 `slot`；按采样器名设置贴图资源 |
| `set_collider` / `get_collider` | `sceneId`、`nodeId`；添加/编辑或查询 box、sphere、capsule 碰撞体及摩擦、弹性、触发器 |
| `set_rigid_body` / `get_rigid_body` | `sceneId`、`nodeId`；添加/编辑或查询运动类型、质量、重力、阻尼和速度 |
| `add_impulse` | `sceneId`、`nodeId`、`impulse` 三分量数组；提交动态刚体冲量，不可撤销 |
| `set_audio_source` / `get_audio_source` | `sceneId`、`nodeId`；添加/编辑或查询 clip 路径、音量、音调、循环和空间衰减 |
| `set_audio_listener` / `get_audio_listener` | `sceneId`、`nodeId`；添加/编辑或查询监听器音量 |
| `play_audio` / `pause_audio` / `stop_audio` / `get_audio_state` | `sceneId`、`nodeId`；播放控制或状态查询，不可撤销 |
| `instantiate_model` | `sceneId`、`path`、`name`；从模型资源创建节点层级，支持撤销重做 |
| `set_animator` / `get_animator` | `sceneId`、`nodeId`；配置独立动画资源、速度、循环及自动播放，或查询配置 |
| `play_animation` / `pause_animation` / `stop_animation` / `seek_animation` / `get_animation_state` | `sceneId`、`nodeId`；播放、保留姿态暂停、恢复静态网格停止、按 `seconds` 定位或查询状态 |
| `set_character_motor` / `get_character_motor` | `sceneId`、`nodeId`；配置或查询核心胶囊角色运动组件 |
| `set_character_controller` / `get_character_controller` | `sceneId`、`nodeId`；配置或查询第一／第三人称输入与相机策略，仅启用对应编辑器插件时注册 |
| `save_scene` | `sceneId`、`path`（相对于项目 `assets/` 的 `.scene` 路径）；保存完整场景及当前网格、材质源 |
| `execute_batch` | `commands` 数组，每项含 `command` 与 `arguments`；接受各模块已注册的可撤销修改，最多 64 项 |
| `undo` / `redo` | `sceneId`；撤销或重做一条编辑记录 |
| `get_editor_receipt` | `session`、`requestId`；查询原请求的持久回执 |

场景与节点 ID 使用查询返回的十进制**字符串**，保留完整 64 位身份。创建后按唯一名称通过 `get_scene` 查询节点；创建或删除的重做可能改变运行时 ID，后续外部请求使用新查询值。`set_material_type` 支持 `builtin/materials/default-pbr`、`builtin/materials/default-unlit`、`builtin/materials/default-blinn-phong` 和项目材质路径；隐藏 Shader 字段不能通过具名参数工具修改。几何创建与拓扑编辑由下方建模模块提供。编辑结果不会自动写回源文件，显式 `save_scene` 才发布场景与资源。

```sh
python3 scripts/mcp_client.py --server build/ninja-engine-debug/mcp_app --project projects/showcase --runtime-dir build/editor-runtime call get_scene
# edit.json 使用查询到的 sceneId/nodeId，内容例如：
# {"sceneId":"1","nodeId":"42","position":[1,0,0]}
python3 scripts/mcp_client.py --server build/ninja-engine-debug/mcp_app --project projects/showcase --runtime-dir build/editor-runtime call set_transform --arguments-file edit.json
```

通用 `call <工具名>` 接受 `--arguments '<JSON对象>'` 或 `--arguments-file <文件>`，二者互斥，省略时传空对象。示例 ID 只演示格式，实际操作必须使用当前查询值。

场景工具经 `editor-command.json` 交由编辑器主线程执行，MCP 不直接访问运行场景。可撤销修改生成编辑记录；保存与截图是独立文件操作，不进入撤销批次。批量修改失败按相反顺序回滚，成功批次只占一条记录；历史最多 128 条。停止、重启或切换场景后旧历史失效。玩法若已修改目标值，撤销会返回冲突，避免覆盖后来的业务状态；例如示例 Game 持续旋转方块时，旋转编辑可能产生此类冲突。

提交使用操作系统文件锁 `editor-command.lock`，进程退出自动释放；当前会话的待完成请求不会被覆盖。请求和回执均核对项目、会话及编号，单文件最多 1 MiB。等待 10 秒仍未确认时返回 `activation: pending` 及原请求身份，表示**结果未确认而非取消**。回执保存在 `editor-receipts/<session>/<requestId>.json`，最新快照为 `editor-receipt.json`；同一 ID 重放不重复执行。查询回执无需编辑器在线；查询成功不代表原修改成功，须检查其中的 `success` 和 `error`。`execute_batch` 的回执命令名为编辑器内部的 `batch`。

编辑器命令模块通过 `EditorCommands::register_command` 登记处理函数，修改返回一个实现 `EditorEdit::apply/revert` 的记录。宿主可借用 `EditorSystem::commands()` 在初始化后、进入主循环前登记模块；全部访问限于编辑器线程。运行时组件保持普通 C++ 接口，不引用 MCP、撤销系统或编辑器。对应类图见[命令执行](docs/editor/class-diagrams/editor-commands.md)及[编辑器 MCP 工具](docs/plugins/editor/class-diagrams/editor-mcp-tools.md)。

### 物理与音频组件

运行时核心使用 `Collider` + `RigidBody` 表达物理对象，使用 `AudioSource` + `AudioListener` 表达音频对象，位置与朝向继续来自 `Transform`。Jolt 与 SDL 句柄只存在于对应系统实现；编辑器负责导入、保存和撤销，MCP 负责工具描述与转发。完整边界见[物理](docs/runtime/physics.md)、[音频](docs/runtime/audio.md)、[组件命令](docs/editor/class-diagrams/component-commands.md)与[MCP 参数](docs/plugins/editor/class-diagrams/physics-audio-mcp-tools.md)。

`set_collider`、`set_rigid_body`、`set_audio_source`、`set_audio_listener` 可添加节点缺少的组件，支持批量与撤销；`get_*` 要求组件存在。动态模拟可能改变速度，撤销会检查完整组件快照并拒绝覆盖新状态。冲量与播放控制属于瞬态运行请求，不进入撤销批次。音频 `playback` 为 0 stopped、1 pending、2 playing、3 paused、4 failed；播放回执成功只表示请求接受，实际输出须查询后续状态。

音频源目前支持 WAV，转换为 `audio.fbs` 描述的交错 F32 PCM。相邻 `.meta` 使用 `[audio]` 的 `sample_rate`（默认 48000）、`channels`（默认 2）和 `gain`（默认 1），变化参与增量导入。生成物仍为无后缀 `cache/gen/<GUID>`，场景用 clip GUID 建立依赖，构建和同步共用现有资源清单。详见[音频导入](docs/editor/class-diagrams/audio-importer.md)。

基础物理包括盒、球、胶囊，静态、运动学、动态刚体及冲量；动态刚体必须位于场景根节点。音频支持单/双声道、循环、音调、音量、暂停、距离衰减和左右声像。结束项目显式清理物理对象、流和逻辑设备；普通场景替换由各系统在下一次更新按场景身份清理。

[公寓物理与音频示例](docs/editor/apartment-physics-audio.md)复用已有场景与资源，增加静态家具碰撞体、动态落球、相机监听器及原创合成循环音频；示例配置不进入引擎核心。

### 渲染设置与真实帧截图

`RenderCommands` 在编辑器主线程调用渲染器；独立的 `RenderMcpTools` 只登记并转发以下工具：

| 工具 | 参数与作用 |
| --- | --- |
| `get_render_settings` | 无参数；读取当前场景组件组成的渲染快照 |
| `set_render_settings` | 环境光、雾、IBL、AO、`hdrEnabled`、`bloomEnabled`、`bloomThreshold`、`bloomIntensity`、`bloomRadius`、`exposure`、`filmicTonemap`、方向光阴影与级联、`clearColor` 等至少一项；活动场景中修改相应组件并支持撤销、保存 |
| `capture_frame` | `path`，相对于项目 `cache/` 的 `.png` 路径；请求真实引擎帧截图 |
| `get_frame_capture` | 无参数；查询截图 `state` 与文件 `path`，`complete` 才表示文件写入完成 |
| `set_render_stats` | 必填 `enabled` 布尔值；开关 bgfx 自带统计面板，默认关闭 |

`set_render_stats {"enabled":true}` 开启面板，`{"enabled":false}` 关闭。开关在空白待机、场景预览和玩法运行中均生效，切换场景或模式保留当前状态；不进入撤销、不标记场景修改，也不随场景保存。面板直接使用 `BGFX_DEBUG_STATS`，不额外启用逐 View Profiler。

独立的 [PerformanceSystem](docs/runtime/class-diagrams/performance-system.md) 已接入运行宿主，当前没有业务采样逻辑。`ENGINE_ENABLE_TRACY=ON` 在配置期选择并私有链接 Tracy 0.14.1 客户端，使用按需连接模式、不启用崩溃处理器；默认 `OFF` 时不引入 Tracy 依赖。客户端保留上游进程生命周期与后台连接线程，尚未接入 CPU 区域、GPU 或内存跟踪。此选项与 bgfx 面板开关独立。

截图由 bgfx 帧回调输出 PNG，场景必须继续提交帧。编辑器校验相对路径并创建父目录，渲染器仅接受宿主提供的明确路径；MCP 不自行绘制图片。命令回执完成只表示请求已接受，截图自身的 `pending` 还须通过 `get_frame_capture` 查询；命令通道的 `activation: pending` 则按原请求身份查询 `get_editor_receipt`，不重复提交。

方向光阴影适配 bgfx `15-shadowmaps-simple` 的 packed-depth 和 16 点 PCF 路径，并参考实际依赖 [bgfx `16-shadowmaps`](https://github.com/bkaradzic/bgfx/tree/7a70927d2af7ffcc5cef9c9e8f4c821e31fcb099/examples/16-shadowmaps)（commit `7a70927d2af7ffcc5cef9c9e8f4c821e31fcb099`）的视锥划分与稳定投影。唯一启用 `cast_shadows` 的方向光支持 1/2/4 级 CSM，使用单 atlas、相机深度选级、交界重叠混合及末级淡出。每级范围按当前观察镜头计算；编辑器隐藏预览镜头同样生效。仍不包含点光或聚光阴影。

`shadowResolution` 为完整 atlas 边长：单级占整图，2/4 级使用半边长 tile，4096 的四级 atlas 每级为 2048。PCF 每个采样点限制在本级 tile 内；View 0..3 为阴影，4/5 分别为 AO 深度预通道与计算，主场景使用 View 6。启用 HDR 时 View 6 写入线性 RGBA16F，View 7..9 完成 Bloom 与最终 SDR 合成，UI 随后在 View 200 绘制。采样器槽 11 保留给 AO，12..14 保留给 IBL，15 保留给阴影，项目材质只使用 0..10。ACES 曲线复用 bgfx `shaderlib.sh`；常量/三色环境光与 IBL 独立设置，可同时贡献光照。渲染设置由场景组件持有，随 `save_scene` 落盘；停止或切换场景不保留前一场景的环境和后处理。

例如 `set_render_settings {"directionalShadows":true,"shadowResolution":4096,"shadowCascades":4,"shadowDistance":150,"shadowSplitLambda":0.6,"shadowCascadeBlend":0.1}`。级联数量仅接受 1、2、4；距离范围 `(0, 100000]`，划分比例范围 `[0, 1]`，混合比例范围 `[0, 0.3]`。最后四项默认分别为 4、150、0.6、0.1；实际阴影距离不超过观察镜头的远裁面。数量和距离等配置可独立修改，保持阴影启用状态。脚本可调用 `Render.SetShadowCascades(count, distance, splitLambda?, blend?)`，或对指定方向灯调用 `Light.SetShadowCascades(...)`；`SetDirectionalShadows` / `Light.SetShadows` 保留已有级联配置。

### 场景环境参数

- 普通节点挂 `EnvironmentComponent`：环境光、三色环境光、IBL、雾与清屏色。一个场景最多一个启用环境。
- 相机节点挂 `PostProcessComponent`：曝光、ACES 色调映射、屏幕空间 AO、HDR 与 Bloom；渲染读取活动相机的组件。HDR 关闭时保留表面 Shader 输出路径，开启时在线性 RGBA16F 场景目标合成，再统一曝光、色调映射与 gamma 编码为 SDR。
- `Light` 保存 `cast_shadows`、`shadow_resolution`、`shadow_bias`、`shadow_cascades`、`shadow_distance`、`shadow_split_lambda`、`shadow_cascade_blend`；当前最多一个启用投影的方向光。天空继续由普通天空网格与材质组成。
- `.scene` 的类型名分别为 `environment`、`post_process`、`light`；参数由 FlatBuffers 生成数据统一承载，导入缓存、保存、重载与运行共用同一份数据。缺少或禁用组件时使用引擎固定默认值。

```toml
[[nodes]]
id = "10"
name = "Environment"
parent = "0"

[[nodes.components]]
type = "environment"
ambient_color = [0.82, 0.87, 1.0]
ambient_intensity = 0.28
ibl_enabled = false
ibl_intensity = 1.0
fog_mode = "exponential_squared"
fog_color = [0.34, 0.43, 0.58]
fog_density = 0.007
clear_color = 0x7496b0ff
```

相机节点可追加 `type = "post_process"`、`exposure = 1.05`、`filmic_tonemap = true` 的组件；方向光组件可设置 `cast_shadows = true`、`shadow_resolution = 4096`、`shadow_bias = 0.0015`、`shadow_cascades = 4`、`shadow_distance = 150`、`shadow_split_lambda = 0.6`、`shadow_cascade_blend = 0.1`。详细接口见[环境与后处理类图](docs/runtime/class-diagrams/environment-components.md)及[渲染类图](docs/runtime/class-diagrams/render-system.md)。

IBL 默认关闭，强度范围 0..1000。环境组件用 `ibl_irradiance`、`ibl_prefilter`、`ibl_lut` 保存三张独立纹理的 GUID；三项全空表示尚未烘焙，即使开关已开启也不产生 IBL 光照。引用非空时必须三项齐全且格式匹配；缺失或损坏时返回失败，不保留旧场景的图。内置受光照材质使用 irradiance cube、随粗糙度选择 mip 的 prefilter cube，以及 BRDF LUT 做 split-sum 采样；无光照材质不应用 IBL。

屏幕空间 AO 默认关闭（`ao_enabled = false`），适配固定 bgfx commit `7a70927d2af7ffcc5cef9c9e8f4c821e31fcb099` 的 [39-assao](https://github.com/bkaradzic/bgfx/tree/7a70927d2af7ffcc5cef9c9e8f4c821e31fcb099/examples/39-assao)，固定 Q2 与两次边缘保持模糊。相机深度预通道、AO 计算和主场景依次提交；结果仅衰减环境光和 IBL，不压暗直射光、自发光、天空或 UI。AO 每帧在 GPU 生成，无需烘焙，不生成贴图源文件、meta 或 GUID 缓存；只有参数随场景保存。关闭效果释放其专属计算资源。

MCP 开启示例：`set_render_settings {"aoEnabled":true,"aoRadius":1,"aoStrength":1,"aoPower":1.5,"aoFadeStart":80,"aoFadeEnd":150}`；关闭只需 `{"aoEnabled":false}`。半径范围 `(0,100]`、强度 `[0,2]`、指数 `[0.1,8]`，世界空间淡出距离要求 `0 <= start < end <= 100000`，默认数值如示例。`.scene` 对应 `ao_enabled`、`ao_radius`、`ao_strength`、`ao_power`、`ao_fade_start`、`ao_fade_end`。脚本可用 `Render.SetAmbientOcclusion(enabled, radius?, strength?, power?, fadeStart?, fadeEnd?)` 或同签名的 `PostProcessComponent.SetAmbientOcclusion(...)`，均保留曝光及色调映射设置；参数错误不部分修改。

HDR 与 Bloom 默认均关闭。实现适配相同固定 bgfx commit 的 [09-hdr](https://github.com/bkaradzic/bgfx/tree/7a70927d2af7ffcc5cef9c9e8f4c821e31fcb099/examples/09-hdr)：九次采样亮部提取、纵向 `blur9`、横向 `blur9` 与最终合成，保留上游模糊权重，适配线性流程和资源生命周期。这里的 HDR 是场景中间缓冲，屏幕仍输出 SDR；已有 `exposure` 显式控制曝光，没有自动测光。场景、天空与 3D 文字参与合成，UI 随后绘制，不受 Bloom 和场景曝光影响。项目表面 Shader 需遵守 `u_tonemap.z` 的线性输出契约，自定义字体 Shader 对应 `u_font_texture.z`；完整约定见 [HDR 与 Bloom](docs/runtime/rendering.md#hdr-场景合成与-bloom)。

MCP 可一次开启：`set_render_settings {"hdrEnabled":true,"bloomEnabled":true,"bloomThreshold":1,"bloomIntensity":0.15,"bloomRadius":1}`；一次关闭：`{"bloomEnabled":false,"hdrEnabled":false}`。阈值作用于曝光前的线性颜色，范围 `[0,100]`；强度 `[0,10]`；模糊半径倍率 `[0.25,4]`，默认分别为 1、0.15、1。`.scene` 字段为 `hdr_enabled`、`bloom_enabled`、`bloom_threshold`、`bloom_intensity`、`bloom_radius`。Bloom 必须依赖已开启的 HDR，非法组合或非有限数值整次失败，原设置不变。脚本通过 `Render.SetHdr(enabled)`、`Render.SetBloom(enabled, threshold?, intensity?, radius?)` 或组件同名方法操作；开启时先 `SetHdr(true)` 再 `SetBloom(true)`，关闭时先 `SetBloom(false)` 再 `SetHdr(false)`，每次检查 bool 返回值。开关不自动修改另一开关；旧曝光及 AO 接口保留 HDR/Bloom 参数。

MCP `set_render_settings` 在活动场景按需建立环境节点、活动相机的后处理组件，并修改投影方向光；这些编辑进入撤销和 `modified` 状态。曝光、AO、HDR 与 Bloom 修改要求场景已指定活动相机，开启阴影要求已有方向光。未加载场景时只修改空白窗口设置，不创建场景，也不影响以后加载的场景。

`Render.Set*` 是修改当前组件的便捷接口，不会替业务创建节点；缺少对应组件时返回 false。推荐先 `Scene.Load`，再通过 `Node.GetComponent(EnvironmentComponent)` / `Node.GetComponent(PostProcessComponent)` 动态修改，或者先用 `AddComponent` 显式创建。原来在 `Scene.Load` 前通过 `Render.Set*` 设置环境的项目应将初值迁到 `.scene`。聚合阴影接口优先操作当前投影的方向光，否则选择首个启用方向光；关闭投影后聚合查询可能显示首灯的阴影参数，逐灯编辑使用 `Light`。

### 场景 GPU IBL 烘焙

`editor/scene_bake` 插件通过 GPU 计算环境贴图的漫反射卷积、粗糙度预过滤及 BRDF LUT，参考 Ant 的 IBL Shader 和 bgfx `18-ibl` 的消费契约，来源保存在 `third_party/notices/ant-scene-bake.md`。运行时只读取生成的纹理；烘焙逻辑不进入运行包。

先打开并保存目标场景预览，确保没有未保存修改或待刷新版本；输入环境纹理必须已导入，有项目源文件及邻接 `.meta`。二维输入按经纬全景解释，也可使用 cube 输入。HDR 输入目前仅支持线性半浮点 KTX1，不直接导入 `.hdr` / `.exr`；普通 PNG/JPG 的 LDR/BasisU 纹理也可作为输入。烘焙要求 GPU 支持计算、半浮点图像写入和纹理读回，同一时间只接受一个作业。

| MCP 工具 | 参数与作用 |
| --- | --- |
| `bake_scene` | 必填 `scene`、`environment` 项目资源路径和 `effects: ["ibl"]`；返回作业 `job` 与当前状态 |
| `get_scene_bake` | 可选 `job`；查询状态、`completed` / `total` 进度及完成后的 `outputs` 路径/GUID |
| `cancel_scene_bake` | 必填 `job`；停止提交新任务，待在途 GPU 读回结束后取消，不发布部分资源 |

例如调用 `bake_scene` 的参数：

```json
{"scene":"project/scenes/temple.scene","environment":"project/textures/sky.ktx","effects":["ibl"],"resolution":128,"samples":256,"lutSize":256}
```

三个可选参数的默认值如上；`resolution` 为 16..512、`lutSize` 为 32..512，均须为 2 的幂；`samples` 为 16..1024。该例使用项目自己的场景与天空，不代表仓库内置这些资源。

请求接受后按帧推进，返回 `running` 不表示已完成。使用 `get_scene_bake {"job":"返回的GUID"}` 查询；只有 `state = "complete"` 才表示 GPU 计算、资源发布、场景引用更新和预览刷新都完成。其余状态为 `idle`、`running`、`cancelling`、`publishing`、`cancelled`、`failed`；失败原因在 `error`。命令通道本身若返回 `activation: pending`，仍按原请求查询 `get_editor_receipt`，不要重复创建烘焙作业。

场景 `assets/scenes/temple.scene` 的结果放在同名目录 `assets/scenes/temple/`：

- `ibl-irradiance.ktx`：RGBA16F 漫反射 cube。
- `ibl-prefilter.ktx`：RGBA16F 粗糙度预过滤 cube，含完整 mip 链。
- `ibl-lut.ktx`：RG16F 二维 BRDF LUT。

每张图都有独立 `.meta` 和 GUID，重复烘焙保留已有身份；缓存仍为无后缀 `cache/gen/<完整GUID>`，场景通过三项 GUID 建立正常依赖并参与打包。作业暂存于 `cache/bake/<job>`。发布前检查场景、输入及目标文件是否变化，冲突时报错；不覆盖并行编辑。

后续优化计划：将与场景无关的 BRDF LUT 改为引擎内置、全局共享的一份资源，避免按场景重复生成；当前保持三份贴图的实现。见[场景烘焙设计](docs/plugins/editor/class-diagrams/scene-bake.md#后续优化全局共享-brdf-lut)。

完成后保留原 `ibl_enabled` 和 `ibl_intensity`，更新场景引用并立即刷新预览。原开关已开启时直接显示新结果；原来关闭时仍关闭，可用 `set_render_settings {"iblEnabled":true}` 开启并保存场景。烘焙只处理输入环境纹理，不采集场景几何，尚未包含场景反射探针、Lightmap 或全局光照烘焙。

GPU 计算和回读按帧推进；发布后的资源导入与预览重建沿用同步流程，大场景刷新期间可能暂停响应。2026-10-31 在 macOS Metal 上验证了 128 分辨率、256 采样的完整 GPU 烘焙、半浮点结果与 GUID 缓存、开启 IBL 后自动应用，以及取消重烘焙不改变已发布文件。构建与相关 CTest 使用 QuickJS Release；该次验证未运行 Debug、Windows 或 Android。

### C++ 建模与 MCP 操作

建模由三个独立目标组成：`engine::modeling` 提供纯数据几何计算，`engine::modeling_editor` 管理场景对象、选择、撤销与项目文件，`engine::modeling_mcp_tools` 只注册和转发工具。宿主负责装配；Engine、Node 和运行时渲染器不依赖建模编辑器。边界、数据所有权和类图见[建模插件](docs/plugins/editor/modeling.md)。

编辑器启动项目后，可通过 MCP 完成模型创建与编辑，无需从 DCC 导入基本几何：

```sh
python3 scripts/mcp_client.py --server build/ninja-engine-debug/mcp_app --project projects/showcase --runtime-dir build/editor-runtime call modeling_help
python3 scripts/mcp_client.py --server build/ninja-engine-debug/mcp_app --project projects/showcase --runtime-dir build/editor-runtime call get_scene
```

取得实际 `sceneId` 后，调用 `modeling_create`（`shape` 与 `parameters`）获得稳定 `modelId`；`modeling_get` 返回当前节点身份、分页顶点/面、选择及镜像状态。`modeling_help` 提供各操作参数示例，`modeling_select` 设置或拓展选择，`modeling_edit` 调用核心操作。复制、组合、分离、删除分别使用 `modeling_duplicate`、`modeling_combine`、`modeling_separate`、`modeling_delete`。编辑和选择共用已有 `undo`、`redo`、`execute_batch`。撤销删除会重新创建节点，因此 `modelId` 保持、运行时 `nodeId` 更新，查询后再调用节点组件工具。

`modeling_save` 将源模型保存到 assets 内的 `.mesh`；`modeling_load` 恢复可编辑几何。保存属于文件操作，不混入可撤销几何批次。源文件中 `[modeling]` 以 FlatBuffers 十六进制存储唯一编辑数据模型，渲染数组供兼容读取；资源导入器以编辑数据重新编译，避免过期渲染数组被发布。保存后先 `stop_project`，再 `sync_assets` 导入和同步；导入管线为新源文件生成唯一邻接 meta，并按现有规则生成 `cache/gen/<GUID>`。启用持续镜像时保存其已生成的完整几何，不保存编辑器会话开关。

完整场景使用 `save_scene({"sceneId":"当前ID","path":"scenes/room.scene"})`。编辑器 `ScenePersistence` 保存节点层级、组件数据、启用状态及活动相机；已加载的网格和没有实例覆盖的材质直接保留原 GUID。仅动态网格和覆盖材质写入相邻 `room-resources/meshes/`、`room-resources/materials/`，覆盖材质使用独立节点槽资源，避免改写共享源。动态网格通过宿主注入的建模回调保留可编辑多边形数据；当前组件修订与磁盘内容未变时复用上次成功导出。所有候选文件比较内容后才替换，不变文件及 meta 保持时间戳，已有 GUID 保留。

回执 `resourceCount` 是实际变化的资源数，包含场景；`meshCount`、`materialCount` 分别统计变化的导出网格和材质，`sceneChanged` 表示场景是否变化，`reusedMeshCount`、`reusedMaterialCount` 表示复用次数。原样重复保存返回 `resourceCount=0`、`importRequired=false`。外部修改与成功保存记录冲突时拒绝覆盖；发布失败恢复本批已写入的源文件、meta 和原时间戳，回滚失败则保留暂存备份并报告位置。

启用资源管线即可注册 `save_scene`；保存已有资源引用不要求建模插件，只有导出动态网格需要网格写入回调。保存本身不加入撤销栈，也不清除编辑历史；可按“编辑 → 保存 → 撤销/重做 → 再保存”只写入差异。在场景预览中，自身保存后的增量导入保留场景与历史；运行项目需要重新部署时使用 `save_scene → stop_project → sync_assets → start_project`，Gameplay 应加载该 `.scene` 路径。保存不自动切换 Gameplay 入口；显式重载后运行时仍消费正常 GUID 资源，编辑器根据 mesh 源路径恢复可编辑数据，选择和撤销历史不落盘。单独 `modeling_save` 只保存一个网格，不保存场景引用和材质覆盖。

Earcut 负责凹面与孔洞三角化，Manifold 负责闭合实体布尔和封口裁切，xatlas 负责 UV 图集展开。前两者及 Clipper2 由 vcpkg 固定版本；xatlas 为固定 Git 子模块，首次构建前执行 `git submodule update --init third_party/xatlas`。不将 Unity 包中的专有实现复制进引擎。

几何验证覆盖体积、封闭性、绕序、孔洞、UV、材质与失败不改变源模型。`modeling.commands` 验证命令事务和身份，`modeling.pipeline` 验证 GUID/meta、权威源编译和增量缓存，`editor.scene_persistence` 覆盖增量保存、共享引用保留、撤销/重做后再保存、外部冲突、失败回滚与重新加载，`modeling.mcp` 在开启 GPU 测试时运行真实编辑器和 MCP。完整能力边界按[功能说明](docs/plugins/editor/modeling.md)评审，不以工具数量替代 ProBuilder/UModeler 的逐项等价验证。

### 现代公寓示例

统一项目 `projects/showcase/` 的 `assets/scenes/apartment.scene` 保存四个区域的开放切面公寓：客厅、卧室、厨房与餐区，包含 322 个可编辑模型、相机和 5 个灯光节点。家具、摆件、倒角、变换、材质与灯光均由 `scripts/create_modern_apartment.py` 通过正式 MCP 工具创建；Gameplay 加载已保存的 `project/scenes/apartment.scene`。

先将统一入口 `projects/showcase/assets/scripts/game.ts` 的导出改为 `./modules/apartment.js`。准备完整 Release 构建后，安装共用的 TS 依赖并启动编辑器（Windows 使用对应 `.exe`）：

```sh
npm ci --prefix projects/showcase/settings
build/ninja-engine-release/editor_app --project projects/showcase --runtime-dir build/apartment-runtime --width 1600 --height 1000
```

保持编辑器运行，在另一个终端启动已保存的场景、应用渲染设置并截图：

```sh
python3 scripts/create_modern_apartment.py --start --view-only --configure-view --view wide --capture screenshots/apartment-wide.png
python3 scripts/create_modern_apartment.py --view-only --view living --capture screenshots/apartment-living.png
python3 scripts/create_modern_apartment.py --view-only --view kitchen --capture screenshots/apartment-kitchen.png
```

使用其他构建目录时通过 `--server <mcp_app路径>` 指定。图片位于项目 `cache/screenshots/`，只有收到截图 `complete` 且 PNG 文件校验通过才报告成功。`--view bedroom` 切换卧室，`--save-only` 保存当前完整场景；不带 `--view-only` 时会按唯一名称复用或创建模型并重新设置示例布局。`--dry-run` 只打印构图清单。MCP 回执默认保存到 `build/apartment-mcp-receipts.jsonl`。

节点、网格、材质、灯光、环境及后处理参数均已落盘；公寓 Gameplay 加载场景即可恢复画面，`--configure-view` 在编辑时修改对应组件，随后通过 `save_scene` 保存。示例使用几何造型与颜色材质，采用实时 PBR、方向光阴影和局部点光；不包含烘焙光照或 IBL。

### 组件、数据模型与资源扩展

- Node 只管理身份、父子关系和 `unique_ptr<Component>` 列表。C++ 使用 `add_component<T>()` / `get_component<T>()`；TS 使用 `AddComponent(Type)` / `GetComponent(Type)`。每个新节点自动具有 Transform，相机、灯光、网格、碰撞体、刚体、音源与监听器也都是组件。
- `include/schemas/runtime/scene.fbs` 只描述场景节点和组件，网格/材质通过 GUID 引用。`mesh.fbs`、`model.fbs`、`skeleton.fbs`、`animation.fbs`、`shader.fbs`、`material.fbs`、`audio.fbs`、`asset_catalog.fbs`、`resource_header.fbs` 独立维护对应模型，`common.fbs` 提供共享数值类型。构建使用宿主 flatc 生成 C++ 到 `build/<preset>/generated/include/engine/runtime/schema/`，生成 TS 到 `build/<preset>/generated/typescript/`；不提交生成物。可用 `ENGINE_FLATC_EXECUTABLE` 指定能在宿主运行的 flatc。FlatBuffers npm 依赖归项目 settings；场景以 `.scene` TOML 文件保存，导入为 FlatBuffers；运行时校验并读取 GUID 二进制。
- 材质和 Shader 的完整资源路径以 `builtin/` 或 `project/` 开头。内置 `builtin/materials/default-pbr` / `default-unlit` / `default-blinn-phong` 分别引用 `builtin/shaders/pbr` / `unlit` / `blinn-phong`；项目可导入自己的 Shader，也可让项目材质复用内置 Shader。两个空间互不覆盖，内置定义不依赖项目。停止项目释放场景、脚本资源引用和 GPU 句柄，最后一个使用者释放后项目数据自动回收；内置定义保留至引擎关闭。
- `mesh.material.SetType(path)` 选择材质定义，`SetShader(path)` 可为实例选择 Shader；`SetFloat/SetV2/SetV3/SetV4(name, ...)` 按声明匹配参数。未声明名字、类型错误、越界数值及未知路径均返回 false，失败保留原值。项目定义来自 `.material` / `.shader` 的导入二进制；这些资源与场景独立维护。运行时修改的材质参数属于当前实例，场景文件保存材质 GUID，不自动回写实例覆盖值。
- ScriptBinding 是绑定接口，各类的 JS 外观与原生回调在 `src/engine/plugins/runtime/script/bindings/` 分文件维护。新增 `xxx_binding.hpp/.cpp` 实现，再在 `script_binding_registry.cpp` 使用 `add<T>()` 登记即可；该目录的绑定源文件由 CMake 自动纳入。公共 BindingContext 处理运行身份、失效引用和参数检查，ScriptSystem 保留脚本加载和执行职责，GameplaySession 驱动部署及重载；核心只依赖 ScriptRuntime 接口。
- Component 统一提供生成数据的 `build_data` / `restore_data`，各具体组件在自己的文件处理恢复与校验，通过 `ComponentRegistry::add<T>()` 登记创建函数。Scene 只组织节点和层级；编辑器 TOML 读写位于资源管线的 `components/`，运行时资源实体解码位于 `runtime/resource/codecs/`。
- EditorEnvironment 借用已校验的项目，配置 App 路径并直接提交导入目录与挂载。ProjectSystem 不提供重复的 root_path 入口；运行时继续通过 App 读取路径语义。

### 资源导入与 GUID

```sh
./build/ninja-engine-debug/asset_build_app --project projects/showcase
```

渲染基础支持 UV0、切线、顶点色、子网格材质槽、动态网格提交、透视/正交相机及最多 64 个方向/点/聚光灯。PBR 的 GGX/Smith/Schlick 实现适配 The Forge `ShaderUtilities.h.fsl`，切线空间采样参考 bgfx `06-bump`；版权及许可保存在 `third_party/notices/`。方向光级联阴影适配 bgfx `15-shadowmaps-simple` 与 `16-shadowmaps`，支持环境漫反射、IBL、屏幕空间 AO、可选 HDR/Bloom 与 ACES 曝光，最终输出 SDR；尚无透明混合与排序。几何生成与编辑归上文的建模插件。

`Material.SetTexture(name, path)` 按 Shader 的 sampler 声明设置贴图，`GetTexture(name)` 返回当前资源路径。内置参数为 `baseColorTexture`、`normalTexture`、`ormTexture`（AO/Roughness/Metallic）；法线与 ORM 使用线性贴图。`MeshRenderer.GetMaterial(slot)` 访问 0..15 的材质槽，`SetMaterialCount(count)` 设置槽数。C++ `set_mesh_data()` 深拷贝生成的 MeshDataT，成功递增修订号并更新 GPU；未落盘的动态网格不能保存成包含无效 GUID 的场景。

材质 `.material` 可设置 `cull = "back"`（默认）、`"front"` 或 `"none"`；脚本以 `Material.SetCullMode()` / `GetCullMode()` 访问同一状态。`none` 用于双面几何，主 pass 和阴影保持相同剔除规则。内置 Shader 通过命名参数 `alphaCutoff`（float，0..1，默认 0 关闭）裁切 `baseColorTexture.a * baseColor.a * vertexColor.a`，适用于叶片和镂空贴图；阴影使用同一阈值。通过 `SetFloat("alphaCutoff", 0.5)` 或 `.material` 的 `[[parameters]]` 设置，不使用专用材质业务字段。自定义 Shader 若采用该阴影契约，须声明同名 `baseColor`（v4）、`baseColorTexture`（sampler）并在主 pass 执行相同裁切。此功能不包含透明混合与排序。

内置 PBR 还支持 `specularWorkflow`（默认 0，保留 metallic）、`specularColor`（v3）、`specularTexture`（RGB 反射色 / A 光滑度）与 `smoothness`（float）；反射色流程的粗糙度为 `1 - smoothness * texture.a`。`normalScale`（默认 1，0..8）调整法线强度，`uvTransform`（v4，默认 1,1,0,0）在主 pass 和阴影中使用同一 UV 缩放偏移。`emissionColor`（v3，默认 0，非负 HDR）乘 `emissionTexture` 添加发光，不创建 Light；显式开启 HDR 与 Bloom 后，高亮发光按线性阈值参与泛光。所有参数通过现有命名接口设置，材质类没有特定来源 Shader 的业务字段。

项目脚本可动态修改环境组件，也可在组件存在时调用 `Render.SetAmbientTricolor(sky, equator, ground, intensity)` 设置三个 `[r,g,b]` 线性颜色；`SetAmbient()` 切回单色环境光。`Render.SetLinearFog(enabled, r, g, b, start, end)` 设置线性雾，要求 `0 <= start < end`。`Render.SetExponentialFog(enabled, r, g, b, density, squared=false)` 设置指数雾，squared=true 为平方指数雾；两种指数模式使用相机前向深度减近裁面。MCP `set_render_settings` 对应支持 `ambientTricolor`、`ambientSkyColor`、`ambientEquatorColor`、`ambientGroundColor`、`fogMode`（none/linear/exponential/exponential_squared）、`fogColor`、`fogStart`、`fogEnd`、`fogDensity`，并保留 `linearFog` 兼容入口。两种效果默认关闭，材质 `fogInfluence` 默认 1，天空可设 0。MeshRenderer 的 `.scene` 字段 `cast_shadows`（默认 true）及脚本 `SetCastShadows()` 控制是否参与阴影拟合和投射；天空应设置 false。

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

KTX 使用独立 `texture_ktx` 导入器，支持线性 RGBA16F / RG16F 的 KTX1 二维或 cube 纹理，单层 mip 或完整 mip 链；不重新压缩或生成 mip。`.meta` 使用 `compression = "none"`、`srgb = false`，`format` 为 `auto` 或与容器一致的半浮点格式；若指定 `mipmaps`，须与源文件一致。生成的 TextureData 保留 KTX 数据，经统一目录加载后由 bgfx 创建纹理。当前不支持 KTX2、纹理数组或体纹理。

Shader `.shader` 中 `[[samplers]]` 声明 `name/uniform/stage/default_texture/exposed`，默认纹理只允许内置资源；项目材质 `stage` 范围为 0..10，必须与 Shader 源码 `SAMPLER2D` 的槽号一致。材质与 UI 的普通纹理引用只接受二维纹理，IBL cube 由环境组件单独绑定。槽 11 及 `s_ambient_occlusion` / `u_ambient_occlusion` 保留给 AO；槽 12/13/14 及 `s_ibl_irradiance` / `s_ibl_prefilter` / `s_ibl_lut`、`u_ibl_params` 保留给 IBL，槽 15 保留给阴影。自定义 Shader 须实现相应采样才能显示这些效果，声明材质参数不会自动获得该效果。bgfx 公共反射能校验名称、类型与数量，不能反射槽号。材质 `.material` 的 `[[textures]]` 用 `name` 与 `texture` GUID 指定覆盖值。示例项目已加入棋盘贴图、UV 和完整 PBR sampler 声明。

编辑器 MCP 增加 `set_camera`、`set_material_texture`；`set_light` 支持 `kind/range/innerAngle/outerAngle`，材质命令支持可选 `slot`。场景查询返回全部材质槽和贴图；修改沿用批量事务、撤销/重做和完成回执。历史只记录资源身份与版本，不让停止项目后的资源因撤销栈继续常驻。

`assets/` 下场景、网格、材质、Shader 描述分别使用 `.scene`、`.mesh`、`.material`、`.shader`，内容为 TOML，不追加 `.toml`。TS、Shader 源码、图片等保留原格式。TS 增量指纹来自编译器实际读取的文件，包括继承配置、额外声明、标准库和编译器自身；导入前后再次核对。Shader 保守依赖项目全部普通源文件，确保自定义 `.inc` 等包含文件变化不会漏编译。每个文件的相邻 `.meta` 记录唯一完整 GUID；新资源自动生成，复制已有 meta 导致冲突时返回失败，移动资源必须一起移动 meta。已知资源丢失 meta 时返回失败，避免静默改变引用。

`cache/gen/` 的直接子文件名只允许完整 GUID，不带扩展名。`cache/catalog.bin` 使用 v2 格式，包含 GUID、逻辑路径、类型、运行依赖、实体头大小、内容哈希、目标平台与存储定位；`cache/assets.sqlite` 是可重建的编辑器索引。删除 SQLite 后根据源文件/meta 重新导入，GUID 不变。导入使用项目锁和暂存/回滚记录，失败不会发布半成品；无变化时保持正式文件与版本不变。

`GuidMap` 位于核心 `runtime/core/`，提供 `path_to_guid` / `guid_to_path`，编辑器与真机共享，不依赖 SQLite。逻辑路径以 `project/` 或 `builtin/` 开头；运行时从资源目录恢复映射。MCP 同步允许没有玩法脚本的纯资源目录；入口是否可运行由启动阶段检查。TaskSystem 默认 4 个导入线程，由 `settings/import.toml` 配置；任务失败会跳过依赖任务并向调用者返回失败。

`SceneImporter::write` 将节点数据写回 `.scene`，`read` 用于导入校验；运行时只读取 FlatBuffers。稳定节点 ID 与运行期实体 ID 分离，父子关系及活动相机在保存/加载后保持。编辑器尚未提供保存按钮；FBX/USD 模型格式由可选编辑器插件导入，未配置专用导入器的普通源文件按字节缓存。已有格式 meta 与当前导入器不匹配时明确失败，不能把已导入模型静默改成普通字节资源。

各资源类型在 `editor/asset_pipeline/importers/` 独立实现 `AssetImporter`，通过 `AssetImporterRegistry::add<T>()` 登记。具体导入器拥有识别、meta 默认值、工具指纹、依赖和转换；管线统一执行增量判断、任务调度、SQLite 与原子发布。TypeScript 每批只编译一次，各脚本复用该批输出。导入器指纹升级为 `engine-asset-importers-7` 后已有项目缓存首次重建一次，上游依赖缓存继续复用。

### FBX / USD 模型资源导入

编辑器把一个 FBX 等美术源文件按“虚拟资源包”展示和组织：`.meta` 的 `[[subresources]]` 分别登记内部资源的 key、类型与独立 GUID，格式导入参数同样保存在 meta。运行时没有这个容器：Model 只保存可实例化的节点层级及 Mesh/Material 引用，蒙皮 Mesh 引用 Skeleton，Animation 独立引用 Skeleton，Material 引用 Texture。场景与业务可以单独使用其中一项；构建按选定入口的实际依赖输出，未使用的子资源不进入包，源文件仍保留。合并存储包只改变独立资源的位置，不扩大加载依赖。

格式解析属于编辑器插件，运行包只读取统一生成资源。`ENGINE_EDITOR_PLUGINS` 分别选择 `fbx_import` / `usd_import`；宿主通过 `AssetPipelineSystem::add_importer` 注册新 id。已有 Mesh 建模转换通过 `set_importer` 替换同一 id；源格式 id 与产物 AssetKind 分开，所以多个格式均可输出 Model。

| 范围 | 职责 |
| --- | --- |
| C++ 运行时核心 | Model/Mesh/Skeleton/Animation 的 FlatBuffers Schema 与单项 ResourceCodec、Scene 实例化、Animator 与 AnimationSystem；核心不读取 FBX/USD 源文件 |
| 编辑器基础 | 源扫描、meta 主/子 GUID、增量依赖、Taskflow、SQLite、独立缓存与原子目录发布 |
| 编辑器格式插件 | ufbx/OpenUSD 解析、源格式设置、子对象枚举、坐标转换与运行数据生成，各插件单独声明依赖 |
| MCP | 使用现有资源导入、打包、同步命令及模型实例化/动画控制命令；协议层不复制模型解析、骨骼采样或场景业务 |

一个 FBX/USD 源的 `.meta` 保存主 GUID 和 `[[subresources]]` 的 `key/kind/guid`。Model 保存节点层级及 Mesh/Material 引用；Skeleton、Animation 独立存在，分别通过实际使用它们的资源引用。每个资源有独立 `cache/gen/<完整GUID>`，无后缀。逻辑路径为 `project/models/person.fbx/meshes/<节点层级>`、`.../skeleton`、`.../animations/<动作名>`；USD 网格使用 `.../meshes/<Prim 路径>`。暂时消失的子资源保留 meta 身份以供再次出现时复用，活跃与保留 GUID 均参与冲突检查。

- FBX 插件使用 MIT 许可的 ufbx v0.23.1，固定提交 `26a482ae66871d7de36eb722aa060bce95bce274`。将坐标统一为米和右手 Y-up，保留根变换；网格包含角点拆分后的 UV、顶点色与最高四项归一化蒙皮权重，骨架包含逆绑定矩阵，动画按 meta 的 `sample_rate`（默认 30）采样。仅支持线性/刚性蒙皮，双四元数、混合形状和几何缓存明确失败；源材质和贴图尚未转换，使用内置 PBR 材质，场景可覆盖项目材质。
- FBX 动画源可在 meta 用 `skeleton = "project/models/person.fbx/skeleton"` 指定另一个 FBX 骨架；空值为本源独立骨架。外部动画按完整节点层级对应关节，必要加权骨骼或祖先缺失返回失败，未动画的可选末端保持绑定姿态。示例 `projects/showcase` 在编辑器导入可再分发的 PAB 模型和 jog/run/walk 动作；默认运行清单只选 walk，jog/run 和源静止 Take 不进入运行包。来源和许可保存在项目内。
- USD 插件独立引入 `usd-import` vcpkg feature。当前只接受自包含的静态 Xform/Scope/Mesh，网格要求 `subdivisionScheme = "none"`、三角形或凸且共面的多边形；世界变换烘焙入顶点，生成平面法线和内置 PBR。外部 layer/reference/payload（包括缺失引用）、动画、实例、细分、洞、未知几何及 Material/Shader 节点明确拒绝。此范围不代表完整 USD 场景导入。

后续格式增加自己的编辑器插件、独立 CMake 清单及 `AssetImporter::id/matches/discover/convert` 实现即可。`discover` 只描述稳定子资源 key 和类型，GUID 分配与发布仍由管线处理。尽量输出已有运行 Schema；新增运行语义时先评审 Schema/ResourceCodec 契约，再增加核心单项能力，不把源格式 SDK 或编辑器设置传入运行时。类图见 [FBX](docs/plugins/editor/class-diagrams/fbx-importer.md)、[USD](docs/plugins/editor/class-diagrams/usd-import.md)和[模型资源](docs/runtime/class-diagrams/model-resources.md)。

### 动画与第一／第三人称测试场景

统一项目 `projects/showcase` 的默认庭院场景包括铺装、建筑立面、台阶、木廊架、长椅和绿植，保留带物理的 Player 与 PAB 蒙皮角色。基础色、法线与 ORM 贴图来源和处理见 [庭院资源说明](projects/showcase/courtyard-source.md)。庭院场景内的环境组件、相机后处理组件及灯光保存环境光、曝光、背景色和方向光阴影，Gameplay 只负责加载场景及角色业务。完整引擎桌面预设默认装配 FBX 导入与角色控制器；显式选择插件时需要 `runtime/resource_graph`、`runtime/script`、`runtime/character_controller` 及相应编辑器导入、控制器与 MCP 插件。

```sh
npm ci --prefix projects/showcase/settings
cmake --preset ninja-engine-debug
cmake --build --preset ninja-engine-debug
./build/ninja-engine-debug/editor_app --project projects/showcase --runtime-dir build/animation-runtime
```

编辑器待机后，在另一终端启动项目：

```sh
python3 scripts/mcp_client.py --server build/ninja-engine-debug/mcp_app --project projects/showcase --runtime-dir build/animation-runtime start
```

聚焦引擎窗口后，WASD 移动，空格跳跃，按住鼠标右键转向，Tab 切换第一／第三人称。第一人称临时隐藏自身模型，切回恢复；第三人称相机通过球体检测避开墙体。使用同一命令的 `stop` 停止并释放场景。Windows 使用对应 `.exe`。

`CharacterMotor` 是核心物理组件，封装 Jolt 胶囊角色运动；输入和相机策略属于可选 `character_controller` 运行时插件。模型由 `Scene.InstantiateModel` 展开为普通节点，网格节点添加 `Animator`，`AnimationSystem` 使用 ozz 采样与 CPU 蒙皮，渲染与阴影共用变形后的顶点。动画播放、暂停、定位和项目启停均可经 MCP 控制。

示例固定循环播放 walk 以检查蒙皮，静止时也会播放；尚未提供按移动速度混合的动画状态机、Root Motion 或 IK。手机端复用控制器的 `set_input`，本场景未制作触屏摇杆。FBX `.meta` 的 `normals` 默认为 `"import"`；本样例源法线反向，显式用 `"generate"` 调用 ufbx 按拓扑和平滑组重新生成，源 FBX 不变。

庭院由 MCP 创建并保存为 231 个节点、225 个环境 Mesh、47,004 个环境三角形和 31 个静态碰撞体；三棵树共 840 片薄叶，每树合为一个 Mesh。人物由 Gameplay 在加载场景后实例化，避免保存后重复生成。项目启动直接读取建图脚本保存的场景组件参数；截图和动作采样来自真实渲染器。

组件扩展也由宿主装配：`RuntimeSetup` 在世界初始化前登记组件和追加可选子系统；`WorldSystem::component_registry()` 提供工厂登记借用。编辑器公开 `SceneComponentCodec` 及显式注册函数，插件拥有自己的 TOML 转换。`EditorSystem::scene_commands()` 提供共享节点身份入口，插件事务复用撤销重建后的 ID；核心组件不感知 MCP、撤销或编辑器模式。

### 资源运行包

```sh
./build/ninja-engine-debug/asset_build_app --project projects/showcase --manifest settings/resource-build.toml
# 可重复 --item；省略时选择清单的全部构建项。
./build/ninja-engine-debug/asset_build_app --project projects/showcase --manifest settings/resource-build.toml --item game
```

项目清单在 `settings/`，指定目标平台、`cache/build/` 下的输出位置、入口与显式保留项。构建按运行依赖计算闭包，支持 `storage = "files"` 的独立 GUID 文件和 `storage = "package"` 的未压缩合并包；统一 `catalog.bin` 为每个 GUID 保存一个实际位置。构建输入依赖仍归 SQLite 增量导入，Shader 源码与 TypeScript 声明不会因为参与编译而自动打进运行包。未变化的文件和包直接复用，失败保留上一份成功发布。

MCP 提供 `build_resources`，参数为 `manifest` 和可选的 `items` 数组。`sync_assets` 可传 `source`（例如 `cache/build/showcase`）同步已构建目录；省略时先导入并同步完整编辑器目录。同步成功只表示候选部署可用，是否激活由运行宿主与热重载开关决定。

命令行客户端使用同一工具协议：

```sh
python3 scripts/mcp_client.py --server build/ninja-engine-debug/mcp_app --project projects/showcase --runtime-dir build/minimal-runtime build --manifest settings/resource-build.toml --item game
python3 scripts/mcp_client.py --server build/ninja-engine-debug/mcp_app --project projects/showcase --runtime-dir build/minimal-runtime sync --source cache/build/showcase
```

编辑器直接使用 `cache/gen`，不要求打包。会覆盖该目录的导入或构建要求先 `stop_project`；运行中会返回明确错误。同步已有的独立运行包不覆盖该缓存，可以在关闭热重载时准备候选版本。`cache/gen` 同时只保存一个目标平台的产物，构建 Android 后要先重新导入宿主平台产物，再启动桌面项目。

资源核心只接收调用方准备好的目录和挂载，提供 GUID/VFS 与单项同步加载，不扫描工程、不发现发布版本。调用方通过 ResourceLoader 请求顶级资源；启用的 ResourceGraphSystem 插件遍历必要依赖、共享缓存并在最后引用释放时反向释放依赖。目录更新成功后原子换代，新请求只使用新代际，已有引用及其依赖仍存活至释放；失败保留旧目录。读目录、读实体头、加载载荷是分开的操作。v1 目录需重新导入，不能直接作为 v2 运行包。接口与文件格式见[资源加载](docs/runtime/resource-loading.md)和[资源构建](docs/plugins/editor/resource-build.md)。

### Android APK

Android 平台工程位于 `platforms/android/`，沿用 [SDL3 的 Activity / JNI 框架](https://wiki.libsdl.org/SDL3/README-android)。原生部分仍由根 CMake Presets 编译，Gradle 只处理 Java、Manifest 和 APK；SDLActivity 源码从同版本 SDL3 的 vcpkg overlay 安装结果取得。APK 直接启动引擎，不包含编辑器项目目录、项目校验或编辑器 UI。

准备 Python 3、JDK 17、Android SDK Platform 35、Build Tools 36.1.0、NDK 27.2.12479018，以及上文的 CMake / Ninja / 编译缓存 / vcpkg。设置 `ANDROID_HOME`、`ANDROID_NDK_HOME`、`JAVA_HOME` 和 `VCPKG_ROOT` 指向已有安装；可复用其他工程的安装目录。Android Gradle Plugin 固定 9.0.0，wrapper 固定 Gradle 9.1.0 并核对官方 SHA-256；也可通过 `GRADLE_HOME` 复用相同版本，脚本会检查版本。

```sh
npm ci --prefix projects/showcase/settings
python3 scripts/build_android.py --config debug --project projects/showcase --asset-builder build/ninja-engine-debug/asset_build_app
python3 scripts/build_android.py --config release --project projects/showcase --asset-builder build/ninja-engine-debug/asset_build_app
```

Android 默认使用 QuickJS；脚本依次调用 `android-arm64-debug` / `android-arm64-release` 的根 configure、显式 `-prepare`（`engine_script_prepare`）、build Preset，再打包。传入 `--backend v8` 使用独立 `android-arm64-v8-debug` / `android-arm64-v8-release` 预设，不混用后端缓存。V8 的 Android 构建入口支持 macOS ARM64 / x64 和 Linux x64 宿主，Windows 宿主的 Android V8 工具链尚未接入。产物为 `output/android/arm64/debug/runtime/engine-showcase.apk` 和 `output/android/arm64/release/runtime/engine-showcase.apk`；最低 Android 9 / API 28，ABI 为 `arm64-v8a`，需要 Vulkan。两种 APK 均使用本机调试签名，供安装测试；发布签名尚未配置，签名密钥不进入仓库。

两种 APK 内均只有 `lib/arm64-v8a/libmain.so`，SDL3、所选 QuickJS 或 V8、引擎及 NDK C++ 运行库静态链接在其中；Java 只加载 `main`，链接器显式保留 SDL 的 `JNI_OnLoad`。Gradle 去除原生库调试符号以控制安装体积；Debug 保留断言与可调试属性。完整的 Debug 原生符号保存在对应构建目录的 `libmain.so`（默认 `build/android-arm64-debug/libmain.so`），可用 APK 原生库的 ELF Build ID 与之对应，无需把符号放进设备。

脚本检查 NDK 版本，并在打包前仅移除该配置的旧 APK，避免增量 ZIP 更新留下旧原生库空洞导致安装包膨胀；原生库、Java、Gradle 和依赖编译缓存继续复用。`--project` 指定打包项目，默认 `projects/showcase`，必须具有 `settings`、`assets`、`cache` 三个目录并安装 settings 中锁定的 TypeScript。根 CMake 的 bundle 目标通过指定的宿主 asset_build_app 增量构建资源：项目有 `settings/resource-build-android.toml` 时按该清单构建固定子集，读取构建回执的输出目录；没有该清单时沿用 `--target android` 导入模式。随后只校验并打包运行 catalog 引用的散文件或合并包；不复制 SQLite、TS/meta 或陈旧产物。`--asset-builder` 必须是已构建的桌面工具（Windows 文件名加 `.exe`），路径经 `ENGINE_ANDROID_ASSET_BUILDER` 传给 CMake；省略时只接受已有的 Android 目标 catalog，拒绝桌面 Metal 缓存和旧 `cache/assets`。原生库与上游缓存继续复用。`--skip-native` 仅重打包已生成的原生库及资源，不能同时指定 `--project` 或 `--asset-builder`。

Android 运行时包含 SDL3、bgfx、FlatBuffers、Basis Universal、flecs、Jolt、Recast / Detour、ozz、RmlUi 和所选脚本后端的实际编译与链接。编辑器、toml++ 项目模块和离线资源管线不进入 APK。依赖共享安装在 `build/dependencies/android/`，与桌面安装目录隔离，并保留 vcpkg 二进制缓存。BasisU 的 Android overlay 只关闭宿主命令行工具，保留完整编码 / 转码库。

选择 V8 时使用独立的 `build/v8/android-arm64/` 工作副本和 Debug / Release 缓存，目标平台、NDK、API 与工具链均参与缓存身份，不复用桌面归档。宿主 Clang / Rust / GN 版本由 V8 固定，Android 使用同一 NDK 的 sysroot、compiler-rt 和 libc++ ABI，由 `c++_static` 将 C++ 运行库合入最终产物；关闭 ThinLTO，最终将完整嵌入依赖合并链接进 `libmain.so`。兼容补丁保存在 `third_party/overrides/v8/`，上游子模块保持只读：除 macOS 宿主和外部 NDK 配置外，NDK 27 缺失的 `std::atomic_ref` 由 V8 / simdutf 内部的 Clang 原子指令兼容分支处理，保持原有内存序与 NDK ABI，不混用其他版本的 C++ 标准库。

APK 包含 `assets/project/` 下的编译资源和 `assets/project-manifest.json` 完整清单；默认 QuickJS 及裁剪 V8 均不包含 `icudtl.dat`；不包含项目设置、TS 源码或 Node.js。正常启动初始化 App 与 LogSystem，再由 RuntimeHost 初始化系统并启动项目 Gameplay；无需在手机上传入编辑器项目目录。GameplaySession 校验 APK 部署清单并向资源核心提交目录，将清单导出到持久目录供首次同步计算删除项；JS 直接通过 SDL 读取 APK，不在启动时解压运行资源。

V8 构建才链接可选嵌入测试，在新的 SDL 进程验证 JavaScript、SharedArrayBuffer 原子操作及十六进制 / Base64 转换，并确认默认关闭的 Intl、Temporal、WebAssembly 不存在。QuickJS 包不包含 V8 测试主体；请求该测试时明确报告后端不匹配。这不增加脚本系统的公开接口。

复用已有 ARM64 Android 模拟器，明确指定其序列号后安装和启动：

```sh
adb -s emulator-5580 install -r output/android/arm64/debug/runtime/engine-showcase.apk
adb -s emulator-5580 shell am start -n org.engine.minimal/.EngineActivity
# 有限帧运行，完成后正常退出 Activity。
adb -s emulator-5580 shell am start -n org.engine.minimal/.EngineActivity --ei frames 120
# 仅在安装 --backend v8 构建的 APK 后运行；EngineV8 日志应逐项报告 PASS。
adb -s emulator-5580 shell am force-stop org.engine.minimal
adb -s emulator-5580 shell am start -n org.engine.minimal/.EngineActivity --ez v8_smoke true
```

`emulator-5580` 为命令示例；使用前通过 `adb devices` 确认实际序列号并替换。已有进程要改用不同启动参数时，先用 `adb -s <serial> shell am force-stop org.engine.minimal` 停止本应用，再重新启动；不需要清除模拟器或应用数据。模拟器必须提供 Vulkan；软件 Vulkan 也可验证功能，但不能用来衡量真机 GPU 性能。

### Android 资源同步与热重载

| App 接口             | Android 指向                                                 |
| -------------------- | ------------------------------------------------------------ |
| `builtinAssetsPath`  | `apk:/assets/project`，由 ResourceSystem 读取 APK 的逻辑路径 |
| `persistentDataPath` | 应用私有 `files/`，保存日志、版本和控制状态，重启仍保留      |
| `cachePath`          | 应用私有 `cache/`，存放可重建的临时数据                   |

先启动 Debug APK，让引擎创建状态和内置清单。电脑上的 MCP 使用明确的设备序列号与包名，拒绝隐式选择设备：

```sh
python3 scripts/mcp_client.py --server build/ninja-engine-debug/mcp_app --project projects/showcase --android-serial emulator-5580 --android-package org.engine.minimal status
python3 scripts/mcp_client.py --server build/ninja-engine-debug/mcp_app --project projects/showcase --android-serial emulator-5580 --android-package org.engine.minimal hot-reload on
# 修改项目 assets 下的脚本、场景、材质等资源后同步；无需重新打 APK。
python3 scripts/mcp_client.py --server build/ninja-engine-debug/mcp_app --project projects/showcase --android-serial emulator-5580 --android-package org.engine.minimal sync
python3 scripts/mcp_client.py --server build/ninja-engine-debug/mcp_app --project projects/showcase --android-serial emulator-5580 --android-package org.engine.minimal status
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
prettier --check --tab-width 4 --print-width 100 include/engine.d.ts "projects/showcase/assets/scripts/**/*.ts" scripts/build_scripts.mjs tests/scene_schema_bridge.ts
```

Black 26.10.1、cmakelang 0.6.13 与 Prettier 用于格式维护，不是引擎构建依赖；可在独立工具环境安装。格式检查不能替代命名、模块职责、返回值错误处理及资源释放顺序的代码审查，C++ 修改仍需 Debug / Release 构建和受影响的 CTest。

## 验证范围与限制

截至 2026-10-31，以下表格保留各功能阶段的验证范围；最新跨平台交付验证见[交付目录](#交付目录)。构建使用根 Presets 或继承它们的本机 Presets；未触发 CI。

| 环境 | 已验证范围 |
| --- | --- |
| macOS ARM64 / AppleClang 21 | Debug / Release 完整构建通过；统一项目与 Model v2 改动后，Debug 全量 70 项通过，Release 首轮 69 项通过，修正 FBX 构建测试清单的共享骨架存储冲突后，该项在两配置复测通过，分别覆盖全部 70 项 |
| macOS Metal / CSM 级联阴影 | Debug / Release 的场景数据、资源管线、渲染基础、窗口渲染、编辑命令、脚本运行、资源编解码和 MCP 注册共 8 项定向测试通过；真实 GPU 覆盖阴影像素、1/2/4 级切换、变形投影及释放。神庙固定五机位复拍完成，原图 2560×1600；本轮未重测其他平台 |
| macOS Metal 公寓 | 330 节点、77 碰撞体；小球落地、施加冲量后再次落地；默认 SDL 设备播放、暂停、续播、停止、调整音量和项目重启；真实渲染截图保存成功 |
| macOS Metal 统一项目 | 同一编辑器会话依次切换基础方块、公寓、动画庭院，再返回方块，分别为 3 / 330 / 388 / 3 节点；公寓音频与庭院 walk 动画正常运行，返回方块后显式恢复其渲染参数；各次停止后场景、脚本及编辑历史清空，入口与 meta 恢复且全部源文件哈希不变 |
| Android ARM64 / API 34 软件 Vulkan 模拟器 | 新增物理音频功能的 Debug APK 编译、安装和哈希核对通过；落球、音频状态、三次前后台、有限帧退出、冷重启、包含音频的 MCP 合并包同步及持久版本启动通过 |
| Android 统一项目 / 模型动画 / 角色控制器 | 最终 Debug APK 的 1123 个独立资源实体与 Android 缓存匹配，含三场景、Model v2、一个骨架和 walk，排除 jog/run/静止 Take 及未引用模型；正式包的 Vulkan 画面、双视角、前后台、120 帧退出和冷启动通过；同源探针包覆盖真实蒙皮、移动、跳跃落地及 360 帧释放，MCP 完整目录同步与持久版激活通过 |
| 既有精简插件与 Android Release | 前次模块分层验证中，关闭脚本/V8/建模的桌面 Release 47 项测试通过，Android Debug / Release 完成资源目录和生命周期验证；本次物理音频未重建 Android Release APK |
| 精简插件装配 | 关闭全部插件的 engine_app、关闭编辑器与 V8 的 pipeline-only asset_build_app 均实际编译、链接及启动帮助命令成功；检查链接命令与依赖边界，无反向链接 |
| Windows / Linux / iOS / Android 实体设备 | 未验证本次新增功能的端到端运行 |

架构检查覆盖源码包含方向和实际 CMake 目标的直接、传递链接；核心、编辑器和插件保持单向依赖。物理音频及初版动画阶段已检查自有代码格式、145 块 Mermaid 图和文档链接；统一项目及 Model v2 阶段另对 20 块受影响类图实际解析、渲染，并检查对应源码格式和文档链接。

动画与模型资源回归覆盖子资源身份稳定、增量导入、非法输入保留旧缓存、独立骨架映射、播放/暂停/停止/定位、模型实例化失败清理与撤销重做。FBX 源法线保留与显式重算均有回归；USD 限定范围的导入和拒绝路径独立测试。窗口测试比较真实变形网格与等价静态网格的主画面及阴影，检查瞬态隐藏/恢复与 GPU 资源释放。桌面通过 MCP 运行含真实 PAB 模型的 388 节点庭院，比较两个采样时刻的实际像素，验证地面及两级台阶落地、切换视角并停止、重新启动。最终画面采用 4096 阴影图与 0.0015 bias；32 个真实渲染姿态编码为演示视频，视频帧率不代表引擎性能。统一项目最后的入口修正另通过 Debug `engine.script_tooling` 与 Release `editor.project_lifecycle` 回归。

物理回归覆盖盒/球/胶囊接触、静态/运动学/动态体、固定步进、单次冲量、触发器、父变换约束、极小尺度、旋转奇异角度以及清理恢复。音频回归覆盖 WAV/meta 增量导入、损坏输入保留旧缓存、PCM 实体校验、多个声源、循环、暂停、修改参数、组件删除、换场景和释放；自动化输出使用 SDL dummy。MCP 在真实编辑器中完成添加组件、方块落地、音频控制、场景保存、重新导入和再次启动；组件历史测试覆盖批量回滚、无效参数原子性和节点重建后的身份关联。

公寓保留原有家具与灯光，新增 76 个静态家具/建筑碰撞体、一个动态球和原创循环音频。通过 MCP 施加冲量后球心最终稳定在约 0.255 米，符合地面高度与球半径；停止后场景释放，再次启动恢复播放。音频实际打开了桌面默认 SDL 输出设备，但这些检查不代表主观听觉评测。

Android APK 的资源包包含 kind=7 / EAUD 音频实体，原生包未包含编辑器、建模或 MCP 插件。设备端落球接触后速度归零，播放、暂停、续播、停止和重播状态正常；360 帧运行正常退出。MCP 同步后的 catalog 与包文件逐字节匹配，冷启动使用已发布持久版本。日志未发现崩溃、ANR 或图形设备丢失；模拟器关闭宿主音频输出，不代表实体扬声器验证。

验证记录和截图位于忽略的 `build/physics-audio-*.log`、`build/physics-audio-apartment/`、`build/physics-audio-android/` 与 `build/plugin-layout-validation/`。Android 测试结束恢复资源版本指针并清理测试版本，只关闭本次启动的独立模拟器；已有模拟器和编辑器保持不动。验证产物不提交，桌面复跑入口保留在 Presets、`tests/` 和公寓示例脚本。

模型与动画的本机验证记录位于忽略的 `build/animation-*.log`、`build/animation-playground/` 和 `build/model-animation-android/`。桌面实景截图保存到 `projects/showcase/cache/screenshots/`，不提交生成缓存。

统一项目、独立模型子资源和庭院的本轮回执位于忽略的 `build/animation-courtyard/`：`verified-final/report.json` 记录实景及生命周期，`verify-samples-release-3/report.json` 记录四次样例切换，`showcase-preview.html` 内嵌真实截图和动画视频。最终原始帧位于 `projects/showcase/cache/courtyard-verification/`；独立 GUID、meta 冲突和项目路径检查均通过。

动画 Android 包包含 Model、Skeleton、Animation 实体及 ozz 蒙皮和运行时控制器；包内容检查未发现编辑器、MCP、FBX/USD 导入实现。MCP 同步核对设备文件和 active/available 版本回执，测试结束恢复原版本指针。此轮只使用独立测试模拟器，不修改其他已运行模拟器。Android 新增动画与控制器仅验证 Debug/API 34 ARM64 模拟器，未重建 Release 或验证实体机。统一项目的最终 APK、资源及设备回执位于 `build/showcase-android-validation/`；无探针正式资源 APK 与带状态日志的测试 APK 分别验证。冷启动在 Gameplay ready 后约 4.51 秒观察到首个庭院画面，固定 2 秒的截图早于首帧，后续校验等待实际出图。MCP 激活检查等待当前进程的新状态，不能把上一次进程残留的 builtin 报告当作最终结果；正式 catalog 和合并包上传后逐字节一致，确认新版本激活再清理测试部署。

限制：

- Android APK 均为本机调试签名。当前单 `libmain.so` 包的 ZIP 与 ELF LOAD 16 KiB 对齐检查通过；尚未在 16 KiB 内存页设备上运行验证。
- adb 同步只支持 Debug 包；正式发布签名和内容更新渠道未实现。FNV-1a 哈希用于完整性检查，不提供签名认证。
- 软件 Vulkan 不反映真机性能，设备纹理压缩格式矩阵仍需实机验证。
- 固定版本 bimg 的部分带 alpha 图片往返会因 `m_hasAlpha` 元数据不一致导致 `texturec --validate` 失败；已测样本像素一致，元数据兼容问题未修补。
- 阴影覆盖唯一启用投影的方向光，支持 1/2/4 级 CSM，尚无点光/聚光阴影。IBL 来自输入环境贴图，尚无场景几何反射探针或 Lightmap。PNG/JPG/JPEG 经 UASTC/ETC1S 导入二维纹理；半浮点二维/cube 纹理使用 KTX1。动画新增范围见 Animator 与 FBX 模型资源模块；游戏 UI 已支持文档和 MVC 脚本绑定，导航请求与完整编辑器界面未实现。
- 物理暂不支持 Mesh 碰撞体、复合刚体、约束关节或碰撞事件回调；动态刚体不支持父节点，球必须等比缩放，胶囊横向缩放必须一致。音频暂不支持压缩音频、流式长音频、混响或音频总线；自动回归使用 SDL dummy 输出，不等于真实扬声器的听觉验证。
- FBX 尚未转换源材质、贴图、混合形状或双四元数蒙皮；USD 仅支持上述自包含静态范围。动画采用 CPU 蒙皮，尚未实现状态机混合、Root Motion、IK；控制器未附带触屏 UI。
- 建模尚未达到 ProBuilder/UModeler 完整等价：选边倒角限闭合凸体，扫掠未支持闭环路径，UV 只有一个通道；其他范围以[建模功能边界](docs/plugins/editor/modeling.md#几何与持久化边界)为准。

## 目录

所有受版本管理的目录均使用小写；`CMakeLists.txt` 等标准文件名保持原样。

```text
.
├── .github/workflows/  # Windows、Linux、macOS 的构建与测试 CI
├── cmake/              # install/dependencies/plugins/core/editor/platforms 构建分工
├── docs/               # 总说明及 runtime/editor/plugins/{runtime,editor} 文档与类图
├── include/engine.d.ts  # 运行时虚拟 engine 模块的类型声明
├── include/engine/     # runtime/editor/plugins/{runtime,editor} 公开头文件
├── include/schemas/    # FlatBuffers 跨语言资源数据模型
├── platforms/          # 桌面路径实现及 Android 入口、Gradle 与原生打包配置
├── projects/showcase/  # 共用设置、缓存，内含方块、公寓与动画庭院场景
├── references/         # 本地参考文件，内容不纳入 Git
├── scripts/            # 宿主安装、TS 构建、MCP 客户端与 Android 打包工具
├── src/engine/         # 与 include/engine 对应的基础与插件实现
├── tests/              # C++ 与命令行测试
└── third_party/        # Git 子模块、vcpkg overlay 与本地补丁
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
| `ENGINE_RUNTIME_PLUGINS` | 按运行时依赖选择 | 开启运行时依赖时为 `resource_graph`，否则为空；分号分隔，支持显式空列表 |
| `ENGINE_EDITOR_PLUGINS` | 按已启用能力选择 | `modeling;asset_build;asset_sync;mcp` 中满足依赖能力者；支持显式空列表 |
| `ENGINE_ENABLE_GPU_TESTS`           | `OFF`      | 注册需要图形桌面和真实 GPU 的窗口测试                                                         |
| `ENGINE_ENABLE_TRACY` | `OFF` | 编译性能系统的 Tracy 按需客户端；仅占位接入，未添加业务采样 |
| `ENGINE_ENABLE_V8` | `OFF` | 未装配脚本插件时启用独立 V8 验证；脚本插件由 `ENGINE_SCRIPT_BACKEND` 决定依赖 |
| `ENGINE_SCRIPT_BACKEND` | `quickjs` | 脚本插件编译期后端，只接受 `quickjs` 或 `v8` |
| `ENGINE_V8_ENABLE_WEBASSEMBLY` | `OFF` | V8 WebAssembly，全部平台默认裁剪 |
| `ENGINE_V8_ENABLE_I18N` | `OFF` | V8 Intl/ICU，默认不部署 ICU 数据 |
| `ENGINE_V8_ENABLE_TEMPORAL` | `OFF` | V8 Temporal，全部平台默认裁剪 |
| `ENGINE_V8_ENABLE_SYSTEM_INSTRUMENTATION` | `OFF` | V8 系统 instrumentation，全部平台默认裁剪 |
| `ENGINE_ENABLE_SCRIPT_RUNTIME`      | `OFF`      | 默认选择运行时 script 插件；显式插件清单优先，完整桌面 engine 及 Android 预设开启 |

作为 `add_subdirectory()` 子项目使用时，应用与测试默认关闭。使用 Engine 与系统基类时链接 `engine::core`；使用 RuntimeHost 装配运行时时启用运行时并链接 `engine::runtime`；可选插件由宿主另行链接及初始化。各系统也提供单独目标，例如 `engine::project`、`engine::render`；目标统一传递 `include/engine/` 搜索根，调用方使用 `runtime/`、`editor/`、`plugins/runtime/` 或 `plugins/editor/` 开头的头文件路径。Preset 用于独立开发，会显式开启对应系统与测试。

`references/` 用于本地放置参考文件，内容不纳入 Git。部分实现代码可参考该目录，但文档中不得记录其中的具体内容。

详细说明见 [开发与第三方依赖](docs/development.md)，协作约定见 [AGENTS.md](AGENTS.md)。项目采用 [MIT License](LICENSE)。
