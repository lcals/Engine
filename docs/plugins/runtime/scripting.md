# 脚本插件

## 脚本后端选择

`ENGINE_SCRIPT_BACKEND` 只能设为 `quickjs` 或 `v8`，默认 `quickjs`。这是编译期互斥选择：脚本目标只定义 `ENGINE_SCRIPT_BACKEND_QUICKJS` 或 `ENGINE_SCRIPT_BACKEND_V8`，只加入对应适配源码和库，不提供运行时切换，不把两套虚拟机装进同一产物。具体 VM 的头文件和句柄限于脚本插件内部，逐类绑定通过同一个 [ScriptVm](class-diagrams/script-vm.md) 接口工作；Gameplay 生命周期、UI MVC、资源快照及热重载共用实现。

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

## V8 依赖

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

V8 各平台的验证范围与限制见[测试与范例](../../testing.md)。

固定 vcpkg baseline 的 V8 port 不支持 macOS 与 Linux，因此使用固定源码的独立 GN 构建，不把该 port 作为跨平台依赖。

仅选择 V8 时使用独立 GN 工具链及成功构建缓存，不经过 CMake 的编译启动器：校验固定源码、补丁、平台 / SDK / 工具链和 GN 参数后，Debug / Release 分别复用已验证的库和头文件；命中时跳过同步、GN、Ninja 与归档合并。缺失产物或输入变化会使缓存失效，失败的构建不会留下成功标记。V8 自带的固定版本 Ninja 保持独立，不替换为环境安装的版本。

V8 在 Windows、macOS、Linux、Android 等平台统一默认关闭 WebAssembly、Intl/ICU、Temporal 和系统 instrumentation（包括 Windows ETW），保留 Sparkplug、Maglev、TurboFan 等 JIT、指针压缩、sandbox 与既有编译优化等级。对应 CMake 开关为 `ENGINE_V8_ENABLE_WEBASSEMBLY`、`ENGINE_V8_ENABLE_I18N`、`ENGINE_V8_ENABLE_TEMPORAL`、`ENGINE_V8_ENABLE_SYSTEM_INSTRUMENTATION`，全部默认 OFF。项目脚本不能使用已关闭的全局 API；默认没有 ICU 资源部署。裁剪参数参与缓存身份，精简归档与头文件存放在带 `-f0000` 的独立目录；此前完整配置的缓存保留。

## 玩法入口

运行 SDK 位于 [engine.d.ts](../../../include/engine.d.ts)，由虚拟 ES 模块 `engine` 提供：

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

每次玩法运行只有一个项目入口，可直接承载业务；它独立于节点。`OnInitialize` 必须返回布尔值。`Debug.Log`、`Debug.LogWarning`、`Debug.LogError` 接受字符串并映射到原生日志。`Scene.Load(path)` 从当前目录按需加载场景及必要依赖，`Scene.FindNode(name)` 查找节点；`Scene.Create` 可以动态创建所属玩法会话的场景；`Scene.CreateNode` 创建节点，节点可以添加 `Camera`、`Light`、`MeshRenderer`、`Collider`、`RigidBody`、`AudioSource`、`AudioListener` 组件。物理与音频接口见[脚本类图](class-diagrams/physics-audio-script-api.md)。全部场景对象都遵守这个结构。节点管理父子关系，Transform 提供局部位置、旋转与缩放；`Time.deltaTime` 为当前 Update 的秒数。完整用法见 [示例 Game](../../../projects/showcase/assets/scripts/game.ts) 与 [SDK 声明](../../../include/engine.d.ts)。停止或重载后旧句柄失效，失败返回 false/null。

## JSON 配置资源

项目配置可保存为独立 `.json` 资源。编辑器插件 `json_import` 校验 1 字节至 4 MiB 的严格 UTF-8 JSON，拒绝 BOM、原始 NUL、注释和尾逗号；成功后原样进入 GUID 缓存、同步与运行包。运行时复用普通资源字节，不向核心增加 JSON 解析器。显式设置 `ENGINE_EDITOR_PLUGINS` 的已有构建需在原清单追加 `json_import`，默认清单已包含该插件。

资源类型表参与公共导入指纹，首次升级会重新导入旧项目缓存；之后继续按资源增量构建，不需清空缓存或重编第三方依赖。

脚本在 `Gameplay.OnInitialize` 等有效回调内调用 `Json.Load("config/maps.json")`，得到 `unknown` 类型的独立 JavaScript 值，并由项目校验字段。模块顶层、构造阶段、缺失路径、非 JSON 资源和非法载荷返回 `null`，合法 JSON `null` 同样返回 `null`。JSON 内的字符串不会自动成为资源依赖；精简包须在 `settings/resource-build.toml` 显式列出配置及其使用的场景、模型等入口，UI 图片继续在 `.rml.meta` 声明。接口及边界见 [JSON 导入](../editor/class-diagrams/json-importer.md)与[脚本读取](class-diagrams/json-script-api.md)。

## 项目结构与模块限制

TS 配置、package 与锁文件放在项目 `settings/`，源码及其他资源放 `assets/`；生成物位于 `cache/gen/<完整GUID>`，不提交缓存和 `node_modules/`。运行模块标识使用 `.js` 后缀。

脚本源码及部署控制 JSON 文本上限为 4 MiB，单次模块图最多 256 个模块。仅支持相对静态 ES import 与虚拟 `engine` 模块，不提供动态 import、顶层 await、Node.js API，也不自动打包 npm 运行时依赖。

启动、同步及热重载流程见[资源同步](../editor/resource-sync.md)，输入见[输入模块](../../runtime/input.md)，路径与KV见[持久化](../../runtime/storage.md)。

## 增加脚本绑定

ScriptBinding 是绑定接口，各类的 JS 外观与原生回调在 `src/engine/plugins/runtime/script/bindings/` 分文件维护。新增 `xxx_binding.hpp/.cpp` 实现，再在 `script_binding_registry.cpp` 使用 `add<T>()` 登记即可；该目录的绑定源文件由 CMake 自动纳入。公共 BindingContext 处理运行身份、失效引用和参数检查，ScriptSystem 保留脚本加载和执行职责，GameplaySession 驱动部署及重载；核心只依赖 ScriptRuntime 接口。
