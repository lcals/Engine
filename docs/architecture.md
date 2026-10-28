# 引擎架构总览

本文是各模块规划的入口；项目当前实现状态见 [README](../README.md)。具体决策在对应模块文档中维护。

## 总体关系与原则

引擎主体是编辑器和插件系统的基础；编辑器与插件系统并列，编辑器可以调用插件。

引擎主体采用组件化架构，项目按需组合引擎能力；运行时实体通过 ECS 按需添加组件。引擎功能模块与实体组件各自保持清晰边界。

引擎整体采用跨平台实现，运行时必须考虑目标平台之间的兼容性。C++ 引擎主体使用规范、惯用的 C++20，优先使用标准库并尽量控制第三方依赖；依赖统一由 vcpkg 管理，在 `vcpkg.json` 中声明，并在 `third_party/CMakeLists.txt` 中查找。

引擎实现参考 `references/` 目录中的三个项目，并结合本项目已确认的架构与跨平台要求完成设计和实现。

## 模块文档

- [运行时](runtime.md)：SDL3 基础能力、ECS、系统组织与 C++ 并行任务。
- [引擎配置](configuration.md)：本地 TOML 配置与首次启动生成。
- [场景](scene.md)：USD 转节点、节点嵌套、高精度大世界与 flecs 实体映射。
- [跨模块数据](data.md)：FlatBuffers 数据格式与运行缓存的内存映射。
- [渲染](rendering.md)：bgfx、默认材质、PBR、渲染图、Blender 视觉基准与视觉资源热重载。
- [资源管线](asset-pipeline.md)：DCC 适配、USD、依赖分析、Mesh 处理与运行缓存。
- [物理](physics.md)：Jolt Physics 接入。
- [导航](navigation.md)：Recast Navigation 接入。
- [动画](animation.md)：ozz-animation 与 GPU 蒙皮要求。
- [游戏运行时 UI](ui.md)：RmlUi 玩家界面。
- [脚本与插件](plugins.md)：C++ 与 TypeScript、V8、脚本热重载及多线程插件。
- [编辑器](editor.md)：跨平台桌面界面、MVC、MCP 与代码热重载。
- [项目与出包](project.md)：项目目录、项目化启动参数和 CMake 单文件出包目标。

[首个游戏](first-game.md)记录游戏目标；[开发说明](development.md)记录当前构建与依赖维护方式。
