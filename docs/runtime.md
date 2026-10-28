# 运行时模块

运行时是引擎主体的基础，按跨平台方式实现；总体原则见[架构总览](architecture.md)。

运行时启动必须指定项目，启动参数也可调用项目内的 TypeScript 脚本入口；项目约定见[项目与出包](project.md)。

启动时读取[引擎本地配置](configuration.md)，缺失时创建默认配置。

[SDL3](https://www.libsdl.org/) 提供大部分运行时基础能力，输入和音频使用 SDL3。

引擎内置 [flecs](https://www.flecs.dev/flecs/)，采用 ECS 模式，运行时实体按需组合组件。复杂功能组织为系统模块并进一步细分组件，简单功能使用单例实现。引擎内部适合并行的 C++ 任务尽量并行执行。

游戏运行时的 TypeScript 脚本与运行插件由 V8 承载，支持运行中热重载与多线程执行；相关约定见[脚本与插件模块](plugins.md)。

节点场景与 flecs 实体的关系见[场景模块](scene.md)；物理、导航、动画和游戏运行时 UI 分别见[物理](physics.md)、[导航](navigation.md)、[动画](animation.md)与[游戏运行时 UI](ui.md)模块。
