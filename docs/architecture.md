# 引擎架构总览

本文是各模块规划的入口；项目当前实现状态见 [README](../README.md)。具体决策在对应模块文档中维护。

架构评审从[引擎类图索引](class-diagrams/index.md)进入：总图只定义引擎和系统，各具体系统独立成图，并提供完整引用关系。先评审类、职责、关系和公开方法，再由助手实现并同步代码。规划文档记录目标，已经实现的契约以独立类图及 README 的现状说明为准。

## 目录与依赖方向

公开头文件与实现分别在 `include/engine/`、`src/engine/` 下采用相同分层：

- `runtime/`：Engine、公共类型、System、App、日志、资源、窗口、渲染、世界及世界子系统；通用工具位于 `runtime/utils/`。
- `editor/`：EditorSystem 直接位于本层；项目、资源管线和资源同步各有子目录。
- `plugins/`：当前 MCP 位于 `plugins/mcp/`，通过编辑器管线与同步能力工作。

编辑器与插件可使用运行时能力，运行时不反向依赖编辑器或插件。自有头文件路径从上述层级开始，不使用纯文件名或相对目录回跳。脚本公共类型声明统一放在 `include/engine.d.ts`。

模块规划文档位于对应的 `docs/<scope>/`，独立类图位于 `docs/<scope>/class-diagrams/`；跨层级类图入口与完整引用关系保留在 `docs/class-diagrams/`。

## 总体关系与原则

引擎主体是编辑器和插件系统的基础；编辑器与插件系统并列，编辑器可以调用插件。

引擎主体采用组件化架构，项目按需组合引擎能力；运行时实体通过 ECS 按需添加组件。引擎功能模块与实体组件各自保持清晰边界。

引擎整体采用跨平台实现，运行时必须考虑目标平台之间的兼容性。C++ 引擎主体使用规范、惯用的 C++20，优先使用标准库并尽量控制第三方依赖；依赖统一由 vcpkg 管理，在 `vcpkg.json` 中声明，并在 `third_party/CMakeLists.txt` 中查找。

引擎实现参考 `references/` 目录中的三个项目，并结合本项目已确认的架构与跨平台要求完成设计和实现。

## 模块文档

- [运行时](runtime/runtime.md)：SDL3 基础能力、ECS、系统组织与 C++ 并行任务。
- [编辑器本地配置](editor/configuration.md)：工具侧 TOML 配置与默认值生成规划。
- [场景](runtime/scene.md)：USD 转节点、节点嵌套、高精度大世界与 flecs 实体映射。
- [跨模块数据](runtime/data.md)：FlatBuffers 数据格式与运行缓存的内存映射。
- [渲染](runtime/rendering.md)：bgfx、默认材质、PBR、渲染图、Blender 视觉基准与视觉资源热重载。
- [资源管线](editor/asset-pipeline.md)：DCC 适配、USD、依赖分析、Mesh 处理与运行缓存。
- [物理](runtime/physics.md)：Jolt Physics 接入。
- [导航](runtime/navigation.md)：Recast Navigation 接入。
- [动画](runtime/animation.md)：ozz-animation 与 GPU 蒙皮要求。
- [游戏运行时 UI](runtime/ui.md)：RmlUi 玩家界面。
- [脚本与插件](plugins/plugins.md)：C++ 与 TypeScript、V8、脚本热重载及多线程插件。
- [编辑器](editor/editor.md)：跨平台桌面界面、MVC、MCP 与代码热重载。
- [项目与出包](editor/project.md)：编辑器项目目录、引擎独立启动与平台出包目标。

[首个游戏](runtime/first-game.md)记录游戏目标；[开发说明](development.md)记录当前构建与依赖维护方式。
