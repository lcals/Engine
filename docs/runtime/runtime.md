# 运行时模块

运行时提供跨平台系统、场景、脚本与渲染能力，依赖方向见[架构总览](../architecture.md)。它只消费部署后的运行资源，不读取编辑器项目设置，也不依赖编辑器、MCP 或离线导入器。

`Engine` 只负责抽象根系统的注册、生命周期和调度。`System` 独占子系统并统一管理递归初始化、更新与反序关闭。`RuntimeHost` 装配具体系统，负责窗口事件、暂停和渲染帧调度；`GameplaySession` 管理玩法启动、停止、部署目录提交及场景、GPU 资源的清理；CPU 资源随实际使用者释放。可执行宿主可先初始化 ResourceSystem 与选中的 ResourceGraphSystem 插件，再将资源底层借给 RuntimeHost；RuntimeHost 库只引用核心接口，不链接具体插件。领域状态由各自模块提供，不在 Engine 增加脚本或场景查询代理。

引擎初始化不要求项目。编辑器宿主先校验项目并准备资源，再启动运行时；Android 使用包内资源及已发布持久版本。玩法入口在系统就绪后显式启动，见[项目与出包](../editor/project.md)。

`App` 为各平台提供含义一致的内置资源、持久数据和可清理缓存路径；日志默认写入持久目录。SDL3 用于窗口、事件、平台目录与同步文件读取。公开头文件和实现分别位于 `include/engine/runtime/`、`src/engine/runtime/`，各系统按职责分目录。[PathUtils](class-diagrams/path-utils.md) 位于 `runtime/utils/`，提供 UTF-8 路径转换，调用者负责权限、规范化及越界检查。

`WorldSystem` 使用 flecs 管理实体，`Node` 与组件表达场景行为；持久数据以生成的 FlatBuffers 结构为准。物理系统通过 Jolt 同步 Collider/RigidBody 并进行固定步模拟；音频系统通过 SDL3 消费 AudioSource 请求并读取 AudioListener。设备、模拟对象和播放状态归各自系统，结束玩法由会话显式清理。具体范围见[物理](physics.md)、[音频](audio.md)、[导航](navigation.md)、[动画](animation.md)和 [UI](ui.md)。

TypeScript 由编辑器工具编译为 JavaScript，再由可选 V8 脚本插件执行。核心只提供 [ScriptRuntime](class-diagrams/script-runtime.md) 执行契约与 ScriptStatus，宿主通过创建函数装配插件，核心不链接 V8 或 ScriptSystem。插件负责模块预检、执行和 Gameplay 回调，绑定实现按类或模块拆分并统一注册。GameplaySession 决定部署来源、应用目录并协调热重载，脚本插件不自行发现或切换部署目录。运行生命周期在宿主线程执行，未提供多线程脚本插件或任意状态迁移。接口见[脚本插件](../plugins/runtime/class-diagrams/script-system.md)与[玩法入口](class-diagrams/gameplay.md)。
