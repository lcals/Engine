# 引擎架构规划

本文记录已确认的引擎架构与技术选型。资源处理见[资源管线规划](asset-pipeline.md)，首个游戏见[首个游戏](first-game.md)；当前实现状态见[README](../README.md)。

## 总体架构

引擎主体是基础，编辑器部分与插件系统并列；编辑器可以调用插件。插件分为编辑器插件和运行插件，均使用 C# 实现，按单线程模型运行。

引擎整体采用跨平台实现，运行时必须考虑目标平台之间的兼容性。C++ 引擎主体使用规范、惯用的 C++20，优先使用标准库并尽量控制第三方依赖；依赖统一由 vcpkg 管理，在 `vcpkg.json` 中声明，并在 `third_party/CMakeLists.txt` 中查找。引擎内部适合并行的 C++ 任务尽量并行执行。

引擎内置 flecs，采用 ECS 模式。复杂功能组织为系统模块并进一步细分组件，简单功能使用单例实现。场景采用节点模式组织，支持自定义节点类型。

## 运行时与渲染

- [SDL3](https://www.libsdl.org/) 是跨平台基础框架，提供大部分运行时基础能力；输入和音频使用 SDL3。
- [bgfx](https://github.com/bkaradzic/bgfx) 提供跨平台渲染。初期先使用 Vulkan 后端跑通流程，其他渲染 API 后续考虑。引擎使用 PBR 渲染；渲染效果优先参考并复用 [bgfx 官方示例](https://bkaradzic.github.io/bgfx/examples.html)中的做法。
- 渲染流程采用渲染图组织，描述各渲染阶段的输入、输出与依赖；渲染图负责编排，实际渲染由 bgfx 执行。首版先覆盖 PBR 所需的固定流程，再按需求扩展。
- [LeanCLR](https://github.com/focus-creative-games/leanclr) 是 C# 脚本运行时。
- 物理通过第三方插件接入 [Jolt Physics](https://github.com/jrouwe/JoltPhysics)。
- 导航使用 [Recast Navigation](https://github.com/recastnavigation/recastnavigation)。
- 动画通过第三方插件接入 [ozz-animation](https://github.com/guillaumeblanc/ozz-animation)，具体接入方式后续确定；渲染侧必须支持 GPU 蒙皮。

## 编辑器与 MCP

编辑器设计参考现代游戏引擎，支持 macOS、Windows、Linux 和多个独立系统窗口；相关代码使用统一的桌面平台宏进行条件编译。GUI 先采用 Dear ImGui（docking 分支）。

编辑器采用 MVC：Model 管理数据，View 负责界面呈现，Controller 处理用户操作；业务逻辑与 Dear ImGui 界面代码分离。引擎提供 MCP 服务，编辑器功能先实现为 MCP 接口，再将对应能力接入 UI。

多窗口是编辑器目标要求。[Dear ImGui 官方说明](https://github.com/ocornut/imgui/wiki/Multi-Viewports)指出，其 Multi-Viewports 在 Linux Wayland 下不工作，在部分 X11 窗口管理器下也有问题；Linux 多窗口方案需要单独设计和验证。

## 项目与出包

项目包含资源目录、生成缓存目录和项目设置目录。资源部分采用引擎自有格式，具体格式后续确定；资源输入与缓存流程见[资源管线规划](asset-pipeline.md)。

项目出包通过传入项目名称触发，直接调用 CMake 完成对应项目的构建与打包。
