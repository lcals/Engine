# 引擎类图索引

类图是架构评审入口；新增类或改变公开契约先评审，再一次更新实现与调用方。当前只维护一个版本，不保留兼容 API。具体规则见[架构](../architecture.md)与[协作约定](../../AGENTS.md)。

每个类的方法、状态、所有权及完整引用关系只在所属图定义一次；跨图只引用。待评审或尚未实现的内容必须在所属图明确标注，不能由索引推断已实现。验证结果统一见[测试文档](../testing.md)，不在本页累积逐次迁移记录。

- [跨模块关系](references.md) · [插件选择与依赖](../plugins/plugins.md) · [构建](../building.md) · [维护](../development.md)

核心命名空间为 `engine`，编辑器为 `engine::editor`，插件为 `engine::plugins::<插件名>`。`*--` 表示独占拥有，`o--` 表示共享资源引用，`-->` 表示借用，`..>` 表示参数、返回值或使用依赖。

## WebGPU 运行产物

范围仅为 Web 游戏导出，编辑器保留原生后端。不新增核心公开类、System 或项目脚本接口；复用运行宿主、资源管线与渲染链。必要共享改动包括目标互斥、私有文件发布及设备暂缓传播，影片呈现使用已有三态返回值。

| 评审项 | 唯一设计入口 |
| --- | --- |
| 共用入口、资源与任务所有权 | [Web 运行宿主](../runtime/class-diagrams/web-host.md#共用游戏入口) |
| 固定上游接入、设备与线程归属 | [GPU 接入前提](../runtime/class-diagrams/web-host.md#gpu-接入前提) |
| 异步设备申请、暂缓与回调收尾 | [WebGPU 表面](../runtime/class-diagrams/web-host.md#异步-webgpu-表面) |
| 浏览器启动、取消与终态通知 | [Web 启动模块](../runtime/class-diagrams/web-host.md#浏览器启动模块) |
| Save 的事务完成与平台适配 | [Web 持久存储](../runtime/class-diagrams/web-storage.md) |
| 全部导入入口与编辑器租约互斥 | [资源管线目标切换](../editor/class-diagrams/asset-pipeline-system.md#目标切换互斥) |
| WGSL 构建、目标切换与包输出 | [Web 导出](../editor/class-diagrams/web-export.md) |

Web 专用入口、启动器、Canvas／设备与存储适配、模板和打包工具集中在 `platforms/web/`；共享宿主与通用契约仍留在所属模块。现有平台参数支持 web，资源 Schema 和项目脚本接口保持不变；原生影片与普通场景共用呈现结果。

## 场景加载与运行观察

场景加载与截图复用运行时链路。下表汇总装配顺序、发布及失败边界、启动结果身份与协议适配的契约入口。

| 职责 | 唯一契约与图 | 行为 |
| --- | --- | --- |
| 镜头初始化 | [相机](../plugins/runtime/class-diagrams/camera-controller.md#职责与所有权)、[宿主装配顺序](../runtime/class-diagrams/runtime-host.md#插件装配完成边界) | 系统先建立安全状态，initialized 回调完成登记后才允许加载与调度 |
| 暂缓与故障 | [渲染返回值](../runtime/class-diagrams/render-system.md#渲染提交结果)、[宿主处理](../runtime/class-diagrams/runtime-host.md) | render 返回 submitted/deferred/failed；暂缓保留内容，实际故障先保存结果再停止 |
| 批次与场景替换 | [加载类图与状态图](../runtime/class-diagrams/scene-loading.md#发布顺序与失败收尾)、[世界拥有关系](../runtime/class-diagrams/world-system.md) | 并行准备、按目标排序发布；首帧前保留旧场景，失效请求进入终态 |
| 启动完成与回执 | [启动快照与接口](../runtime/class-diagrams/gameplay-session.md#启动请求的身份与结果)、[MCP 适配](../plugins/runtime/class-diagrams/mcp-runtime-test.md#状态与工具契约) | 启动增加身份和有界结果查询/释放，命令回执只证明执行或受理 |
| 编辑器通道所有权 | [编辑器端点](../plugins/editor/class-diagrams/editor-mcp-tools.md#独立-mcp-端点)、[编辑器系统](../editor/class-diagrams/editor-system.md) | 宿主拥有端点，端点拥有通道，编辑器提供普通命令与准备推进 |

RenderSubmitStatus 区分提交、暂缓和失败，GameplayStopCause 显式区分主动请求与故障，不解析诊断文字。GameplayPrepareSnapshot 保存启动事实，GameplayPrepareStatus 包含 active/cancelled 终态。启动身份使用非零 uint64_t，范围与帧保持各自身份；测试通过适配访问同一运行时实现。

## 核心与宿主

- [Web 运行宿主与 WebGPU](../runtime/class-diagrams/web-host.md)
- [Android 宿主](../runtime/class-diagrams/android-host.md)
- [应用平台目录](../runtime/class-diagrams/app.md)
- [引擎与系统](../runtime/class-diagrams/engine-system.md)
- [帧率限制](../runtime/class-diagrams/frame-limiter.md)
- [玩法运行会话](../runtime/class-diagrams/gameplay-session.md)
- [项目玩法入口与脚本 SDK](../runtime/class-diagrams/gameplay.md)
- [字节哈希工具](../runtime/class-diagrams/hash-utils.md)
- [日志系统](../runtime/class-diagrams/log-system.md)
- [路径编码工具](../runtime/class-diagrams/path-utils.md)
- [性能观测接入](../runtime/class-diagrams/performance-system.md)
- [运行宿主](../runtime/class-diagrams/runtime-host.md)
- [运行设置与项目配置边界](../runtime/class-diagrams/runtime-settings.md)
- [脚本运行契约](../runtime/class-diagrams/script-runtime.md)
- [运行时线程与任务系统](../runtime/class-diagrams/task-system.md)
- [配置与状态](../runtime/class-diagrams/types.md)

## 资源系统

- [GUID 与逻辑路径映射](../runtime/class-diagrams/guid-map.md)
- [大场景加载与准备](../runtime/class-diagrams/large-scene-preparation.md)
- [场景加载、动态批次与业务状态](../runtime/class-diagrams/scene-loading.md)
- [资源元数据目录](../runtime/class-diagrams/resource-catalog.md)
- [逐类型资源处理器](../runtime/class-diagrams/resource-codecs.md)
- [资源业务调用与模块任务](../runtime/class-diagrams/resource-consumers.md)
- [统一资源生命周期](../runtime/class-diagrams/resource-lifecycle.md)
- [资源加载契约与获取句柄](../runtime/class-diagrams/resource-loader.md)
- [核心资源读取系统](../runtime/class-diagrams/resource-system.md)
- [资源对象与类型承载](../runtime/class-diagrams/resource.md)
- [核心文件类](../runtime/class-diagrams/virtual-file-system.md)

## 渲染与影片

- [大气天空与 Hex-Tiling 材质增量](../runtime/class-diagrams/atmosphere-hex-material.md)
- [场景环境与后处理组件](../runtime/class-diagrams/environment-components.md)
- [分簇前向、反射与间接光](../runtime/class-diagrams/forward-lighting-reflections.md)
- [材质](../runtime/class-diagrams/material.md)
- [影片复用音频](../runtime/class-diagrams/movie-audio.md)
- [影片资源](../runtime/class-diagrams/movie-data.md)
- [影片播放器](../runtime/class-diagrams/movie-player.md)
- [影片启动与宿主分阶段装配](../runtime/class-diagrams/movie-startup.md)
- [渲染扩展](../runtime/class-diagrams/render-extension.md)
- [渲染能力与调试控制](../runtime/class-diagrams/render-feature-controls.md)
- [视图提交、剔除与静态实例化](../runtime/class-diagrams/render-submission.md)
- [渲染系统](../runtime/class-diagrams/render-system.md)
- [按范围准备、首帧与截图](../runtime/class-diagrams/render-readiness.md)

## 场景与领域组件

- [动画时间事件与注入契约](../runtime/class-diagrams/animation-events.md)
- [动画资源与重定向契约](../runtime/class-diagrams/animation-retargeting.md)
- [动画播放、IK 与骨骼挂点](../runtime/class-diagrams/animation-rigging.md)
- [动画系统](../runtime/class-diagrams/animation-system.md)
- [动画组件](../runtime/class-diagrams/animator.md)
- [音频组件](../runtime/class-diagrams/audio-components.md)
- [音频系统](../runtime/class-diagrams/audio-system.md)
- [角色物理组件](../runtime/class-diagrams/character-motor.md)
- [角色物理固定步驱动](../runtime/class-diagrams/character-motor-control.md)
- [组件类型登记](../runtime/class-diagrams/component-registry.md)
- [节点组件](../runtime/class-diagrams/component.md)
- [ECS 场景与虚拟节点](../runtime/class-diagrams/ecs-scene.md)
- [字体系统](../runtime/class-diagrams/font-system.md)
- [输入数据契约](../runtime/class-diagrams/input-data.md)
- [输入系统](../runtime/class-diagrams/input-system.md)
- [字符串 KV 持久存取](../runtime/class-diagrams/key-value-store.md)
- [Web 持久文件发布](../runtime/class-diagrams/web-storage.md)
- [分段静态网格碰撞](../runtime/class-diagrams/mesh-collision-data.md)
- [网格工具](../runtime/class-diagrams/mesh-utils.md)
- [模型资源（Model）、骨骼与动画](../runtime/class-diagrams/model-resources.md)
- [导航系统](../runtime/class-diagrams/navigation-system.md)
- [节点值句柄](../runtime/class-diagrams/node.md)
- [常驻节点与场景切换](../runtime/class-diagrams/persistent-nodes.md)
- [物理组件](../runtime/class-diagrams/physics-components.md)
- [物理系统](../runtime/class-diagrams/physics-system.md)
- [预制体资源与实例](../runtime/class-diagrams/prefab.md)
- [FlatBuffers 共享数据模型](../runtime/class-diagrams/scene-data.md)
- [场景](../runtime/class-diagrams/scene.md)
- [场景界面](../runtime/class-diagrams/ui-system.md)
- [窗口系统](../runtime/class-diagrams/window-system.md)
- [世界系统](../runtime/class-diagrams/world-system.md)

## 编辑器基础

- [Web 运行产物导出](../editor/class-diagrams/web-export.md)
- [资源导入异步批次（规划／待实现）](../editor/class-diagrams/asset-import-jobs.md)
- [逐类型资源导入](../editor/class-diagrams/asset-importers.md)
- [资源管线系统](../editor/class-diagrams/asset-pipeline-system.md)
- [音频导入](../editor/class-diagrams/audio-importer.md)
- [按类型组织的组件编辑命令](../editor/class-diagrams/component-commands.md)
- [编辑器命令与撤销](../editor/class-diagrams/editor-commands.md)
- [编辑器环境](../editor/class-diagrams/editor-environment.md)
- [编辑器系统](../editor/class-diagrams/editor-system.md)
- [编辑器插件扩展工具](../editor/class-diagrams/extension-tools.md)
- [模型实例化命令](../editor/class-diagrams/model-commands.md)
- [预制体编辑与导入](../editor/class-diagrams/prefab.md)
- [项目系统](../editor/class-diagrams/project-system.md)
- [渲染编辑命令](../editor/class-diagrams/render-commands.md)
- [场景组件描述编解码](../editor/class-diagrams/scene-component-codecs.md)
- [场景导入与保存](../editor/class-diagrams/scene-importer.md)
- [活动场景保存](../editor/class-diagrams/scene-persistence.md)
- [场景预览](../editor/class-diagrams/scene-preview.md)
- [静态模型 LOD](../editor/class-diagrams/static-model-lod.md)
- [贴图导入](../editor/class-diagrams/texture-importer.md)

## 运行时插件

- [可选运行时 MCP 与场景测试](../plugins/runtime/class-diagrams/mcp-runtime-test.md)
- [场景加载的脚本业务接口](../plugins/runtime/class-diagrams/scene-loading-script-api.md)
- [动画重定向插件](../plugins/runtime/class-diagrams/animation-retarget.md)
- [动画脚本接口](../plugins/runtime/class-diagrams/animation-script-api.md)
- [TS 动画状态机](../plugins/runtime/class-diagrams/animation-state-machine.md)
- [角色控制、镜头适配与角色表现](../plugins/runtime/class-diagrams/character-controller.md)
- [角色输入脚本接口](../plugins/runtime/class-diagrams/character-input-script-api.md)
- [参数化角色控制](../plugins/runtime/class-diagrams/character-control-design.md)
- [角色能力组合](../plugins/runtime/class-diagrams/character-abilities.md)
- [角色与镜头、动画、脚本衔接](../plugins/runtime/class-diagrams/character-integration-design.md)
- [参数驱动相机插件](../plugins/runtime/class-diagrams/camera-controller.md)
- [相机状态机与配置数据](../plugins/runtime/class-diagrams/camera-state-machine.md)
- [相机 C++ 模块组合与扩展](../plugins/runtime/class-diagrams/camera-modules.md)
- [相机控制脚本接口](../plugins/runtime/class-diagrams/camera-script-api.md)
- [建筑破坏连通图](../plugins/runtime/class-diagrams/destruction-graph.md)
- [破坏脚本接口](../plugins/runtime/class-diagrams/destruction-script-api.md)
- [建筑破坏：Blast 计算与 Jolt 提交](../plugins/runtime/class-diagrams/destruction.md)
- [特效组件脚本接口](../plugins/runtime/class-diagrams/effects-script-api.md)
- [特效插件](../plugins/runtime/class-diagrams/effects.md)
- [动态碎片退休](../plugins/runtime/class-diagrams/fragment-retirement.md)
- [输入脚本接口](../plugins/runtime/class-diagrams/input-script-api.md)
- [JSON 脚本接口](../plugins/runtime/class-diagrams/json-script-api.md)
- [影片脚本接口与 Run 生命周期](../plugins/runtime/class-diagrams/movie-script-api.md)
- [多角色导航](../plugins/runtime/class-diagrams/navigation-crowd.md)
- [导航脚本接口](../plugins/runtime/class-diagrams/navigation-script-api.md)
- [物理与音频组件脚本接口](../plugins/runtime/class-diagrams/physics-audio-script-api.md)
- [在活动玩法中执行临时 TypeScript](../plugins/runtime/class-diagrams/project-script-execution.md)
- [渲染脚本接口](../plugins/runtime/class-diagrams/render-script-api.md)
- [资源生命周期插件](../plugins/runtime/class-diagrams/resource-graph.md)
- [资源脚本接口](../plugins/runtime/class-diagrams/resources-script-api.md)
- [运行时窗口、退出与鼠标脚本接口](../plugins/runtime/class-diagrams/runtime-controls-script-api.md)
- [脚本绑定接口与实现](../plugins/runtime/class-diagrams/script-bindings.md)
- [脚本系统](../plugins/runtime/class-diagrams/script-system.md)
- [脚本虚拟机中间层](../plugins/runtime/class-diagrams/script-vm.md)
- [字符串 KV 脚本接口](../plugins/runtime/class-diagrams/storage-script-api.md)
- [地形脚本接口](../plugins/runtime/class-diagrams/terrain-script-api.md)
- [体积地形](../plugins/runtime/class-diagrams/terrain.md)
- [UI 脚本与 MVC](../plugins/runtime/class-diagrams/ui-script-api.md)

## 编辑器插件

- [动画制作命令与 FBX 元数据](../plugins/editor/class-diagrams/animation-authoring.md)
- [资源构建系统](../plugins/editor/class-diagrams/asset-build-system.md)
- [统一资源导入插件](../plugins/editor/class-diagrams/asset-import.md)
- [资源同步系统](../plugins/editor/class-diagrams/asset-sync-system.md)
- [角色控制器编辑器插件](../plugins/editor/class-diagrams/character-controller.md)
- [角色扩展编辑器接入](../plugins/editor/class-diagrams/character-control-design.md)
- [相机控制编辑器接入](../plugins/editor/class-diagrams/camera-controller.md)
- [建筑破坏预处理](../plugins/editor/class-diagrams/destruction-authoring.md)
- [建筑破坏资源与编辑器命令](../plugins/editor/class-diagrams/destruction-editor.md)
- [可编辑多边形模型](../plugins/editor/class-diagrams/editable-mesh.md)
- [编辑器 MCP 工具模块](../plugins/editor/class-diagrams/editor-mcp-tools.md)
- [特效导入与编辑器工具](../plugins/editor/class-diagrams/effect-importer.md)
- [FBX 模型资源导入](../plugins/editor/class-diagrams/fbx-importer.md)
- [字体资源导入](../plugins/editor/class-diagrams/font-importer.md)
- [JSON 资源导入](../plugins/editor/class-diagrams/json-importer.md)
- [MCP 协议与工具注册](../plugins/runtime/class-diagrams/mcp-system.md)
- [独立运行程序的 MCP 测试接入](../plugins/editor/class-diagrams/runtime-mcp-debug.md)
- [建模几何操作](../plugins/editor/class-diagrams/mesh-geometry.md)
- [参数化基本体](../plugins/editor/class-diagrams/mesh-primitives.md)
- [多边形拓扑操作](../plugins/editor/class-diagrams/mesh-topology.md)
- [建模编辑业务](../plugins/editor/class-diagrams/modeling-commands.md)
- [建模 MCP 适配](../plugins/editor/class-diagrams/modeling-mcp-tools.md)
- [建模资源导入](../plugins/editor/class-diagrams/modeling-mesh-importer.md)
- [影片导入契约与实现范围](../plugins/editor/class-diagrams/movie-importer.md)
- [影片编辑与 MCP 工具（规划／待实现）](../plugins/editor/class-diagrams/movie-mcp-tools.md)
- [多人导航组件编辑器插件](../plugins/editor/class-diagrams/navigation-crowd.md)
- [物理与音频 MCP 适配](../plugins/editor/class-diagrams/physics-audio-mcp-tools.md)
- [项目 MCP 工具模块](../plugins/editor/class-diagrams/project-mcp-tools.md)
- [渲染 MCP 适配](../plugins/editor/class-diagrams/render-mcp-tools.md)
- [场景 GPU 烘焙](../plugins/editor/class-diagrams/scene-bake.md)
- [场景静态照明烘焙](../plugins/editor/class-diagrams/scene-lighting-bake.md)
- [场景预览 MCP 适配](../plugins/editor/class-diagrams/scene-preview-mcp-tools.md)
- [地形资源与编辑器命令](../plugins/editor/class-diagrams/terrain-editor.md)
- [UI 编辑器插件](../plugins/editor/class-diagrams/ui-editor.md)
- [UI 文档导入](../plugins/editor/class-diagrams/ui-importer.md)
- [USD 导入器](../plugins/editor/class-diagrams/usd-import.md)
