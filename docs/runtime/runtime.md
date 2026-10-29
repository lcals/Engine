# 运行时模块

运行时是引擎主体的基础，按跨平台方式实现；总体原则见[架构总览](../architecture.md)。

Engine 初始化不要求项目。编辑器宿主必须接收并校验项目，然后准备运行内容；玩法入口在所有引擎系统就绪后显式启动。运行时只消费部署内容，不读取编辑器项目设置，具体边界见[项目与出包](../editor/project.md)及[完整引用关系](../class-diagrams/references.md)。

App 提供平台内置资源、持久数据和可清理缓存目录；日志默认使用持久目录。公开头文件和实现分别位于 `include/engine/runtime/`、`src/engine/runtime/`，公共类型直接在运行时根目录，系统按职责分子目录。[PathUtils](class-diagrams/path-utils.md) 位于 `runtime/utils/`，只复用 UTF-8 路径转换。编辑器本地配置属于[编辑器配置规划](../editor/configuration.md)，不作为 Engine 启动条件。

[SDL3](https://www.libsdl.org/) 已用于窗口、事件、平台目录和同步文件读取；输入与音频功能按后续需求接入。

引擎内置 [flecs](https://www.flecs.dev/flecs/)，采用 ECS 模式，运行时实体按需组合组件。复杂功能组织为系统模块并进一步细分组件，简单功能使用单例实现。引擎内部适合并行的 C++ 任务尽量并行执行。

当前桌面 TypeScript 编译为 JavaScript 后由 V8 执行，支持项目 Gameplay 和运行会话热重载；脚本生命周期在引擎运行线程执行，不把多线程插件规划视为已实现。当前接口见[脚本系统](class-diagrams/script-system.md)与[玩法入口](class-diagrams/gameplay.md)，插件扩展目标见[脚本与插件规划](../plugins/plugins.md)。

节点场景与 flecs 实体的关系见[场景模块](scene.md)；物理、导航、动画和游戏运行时 UI 分别见[物理](physics.md)、[导航](navigation.md)、[动画](animation.md)与[游戏运行时 UI](ui.md)模块。
