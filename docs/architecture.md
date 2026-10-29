# 引擎架构总览

架构评审从[类图索引](class-diagrams/index.md)进入：总图只定义 `Engine` 与 `System`，各领域类独立维护职责、所有权、依赖和公开方法。类图与代码同步，使用说明及验证范围统一见根 [README](../README.md)。

[资源系统](runtime/resource-loading.md)提供通用目录更新和同步加载，编辑器通过调用核心实现扫描、增量导入与构建业务，真机启动使用固定精简清单。资源与必要依赖由实际使用者共享，目录不持有全部实体数据。

## 调用方驱动核心

这是整个架构的设计原则：玩法业务、运行宿主和编辑器都调用同一套最小核心能力，按各自需要组织流程。核心不决定谁在调用、什么时候应该调用或调用几次，只完成明确请求并返回状态。

| 角色 | 决定什么 | 如何使用核心 |
| --- | --- | --- |
| 玩法业务 | 场景选择、对象创建、业务行为、资源使用时机 | 调用场景、节点组件、资源和脚本等模块的通用接口 |
| 编辑器 | 项目启停、扫描导入、编辑操作、打包和调试流程 | 调用相同核心接口，组合编辑器自己的业务 |
| 运行宿主 | 平台入口、帧推进、部署来源和启动顺序 | 装配系统，调用核心初始化、更新与关闭 |
| 核心 | 每项能力内部的正确执行、状态与生命周期约束 | 提供最小接口，返回成功、失败或必要状态 |

资源加载时自动处理必要依赖属于加载能力本身；是否加载哪个场景、是否启用热重载、何时应用新目录属于调用方决策。窗口、世界、渲染、脚本等模块同样遵循这一划分，不把上层流程塞进 Engine 或任何核心系统。

“编辑器多次更新目录，真机启动提交一次”只是调用频率的例子，不是核心里的两种模式。编辑器与运行时不需要两套资源机制，也不为调用方另建通用 Driver 类。具体所有权和公开接口以独立类图为准。

## 分层与依赖

公开头文件和实现分别位于 `include/engine/`、`src/engine/`，采用相同分层：

- `runtime/`：系统框架、运行时宿主、玩法会话、平台路径、窗口、日志、资源、世界和渲染；公共工具位于 `runtime/utils/`。
- `editor/`：编辑器环境与控制、项目约束、任务调度、资源导入和同步。
- `plugins/`：MCP 协议及工具，通过编辑器模块调用资源管线和同步能力。

编辑器和插件可以使用运行时，运行时不依赖编辑器、插件、离线导入器或具体项目。平台专用实现、入口与打包资源集中于 `platforms/<platform>/`。自有头文件从 `runtime/`、`editor/` 或 `plugins/` 开始包含；CMake 目标传递搜索根，不使用相对目录回跳。

模块说明位于 `docs/<scope>/`，独立类图位于 `docs/<scope>/class-diagrams/`；跨层入口和[完整引用关系](class-diagrams/references.md)位于 `docs/class-diagrams/`。TypeScript 公共声明统一维护在 `include/engine.d.ts`，只暴露已实现绑定。

## 框架与领域职责

`Engine` 只管理抽象根系统的注册、初始化、更新和反序关闭。`System` 独占子系统，统一管理递归生命周期。具体系统装配、窗口事件、暂停与帧调度归 `RuntimeHost`，Gameplay 启停、部署目录提交与场景、GPU 资源的释放归 `GameplaySession`；CPU 资源按实际共享引用回收。脚本状态、场景查询和渲染统计由各自模块提供，不向 Engine 增加便利转发。

运行时启动不要求项目目录。编辑器通过独立 `ProjectSystem` 校验项目，再由 `EditorEnvironment` 准备路径及资源；运行时消费部署后的 catalog 与 GUID 二进制。Android 宿主读取包内或持久版本，不依赖编辑器项目结构。

场景对象统一为 Node 与 Component，Node 只管理身份、层级和组件集合。场景、组件及资源数据由 FlatBuffers schema 定义；GUID 与逻辑路径的双向映射位于核心模块，桌面与设备共用。资源管线负责源文件、meta、SQLite、转换和发布；运行时读取目录元数据，按需加载实体及其必要依赖。

实现采用 C++20 与返回值错误处理，资源所有权和失败清理保持明确。依赖使用固定版本的 vcpkg port 或 Git 子模块，实际参与系统编译和链接。代码规则见 [AGENTS.md](../AGENTS.md)，构建和补丁维护见[开发说明](development.md)。

## 模块入口

- [运行时](runtime/runtime.md)：框架、宿主、玩法和系统生命周期。
- [场景](runtime/scene.md)：Node、组件、层级、变换及保存与加载。
- [跨模块数据](runtime/data.md)：FlatBuffers 模型、GUID 引用与快照。
- [渲染](runtime/rendering.md)：bgfx、平台后端、材质、PBR 与 GPU 资源生命周期。
- [资源管线](editor/asset-pipeline.md)：meta、Taskflow、SQLite、增量转换和完整版本发布。
- [资源构建](editor/resource-build.md)：项目清单、依赖闭包、散文件和合并包。
- [物理](runtime/physics.md)、[导航](runtime/navigation.md)、[动画](runtime/animation.md)、[运行时 UI](runtime/ui.md)：各系统的依赖接入与能力范围。
- [脚本与插件](plugins/plugins.md)：V8 绑定、Gameplay、热重载和 MCP。
- [编辑器](editor/editor.md)、[编辑器配置](editor/configuration.md)：环境、项目控制和工具设置。
- [项目与出包](editor/project.md)：项目目录契约、运行内容与平台部署。
- [首个游戏](runtime/first-game.md)：玩法目标及引擎验证需求。
