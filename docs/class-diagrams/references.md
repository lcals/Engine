# 跨模块引用关系

本页只说明跨模块关系并链接唯一的契约定义；类、字段、方法、拥有与借用的完整关系在所属类图维护，避免两份 Mermaid 同步漂移。整体分层见[架构](../architecture.md)，全部类图见[索引](index.md)。

## 构建边界

运行时核心、编辑器基础、运行时插件、编辑器插件保持单向依赖。宿主选择并装配具体插件，核心与业务使用可替换契约；同一构建只包含一个脚本后端。

契约：[include/engine.d.ts](../../include/engine.d.ts) · [插件登记](../plugins/plugins.md) · [Android 宿主](../runtime/class-diagrams/android-host.md)。

## Web 游戏导出

Web 宿主与原生宿主共用游戏入口状态和 RuntimeHost；Web 平台适配负责浏览器调度、路径及 GPU 申请，资源仍由同一目录、加载器与渲染准备链消费。离线导出由现有资源构建和 ShaderImporter 扩展 web 目标，桌面编辑器不改用 WebGPU。实际验证范围见[测试文档](../testing.md#webgpu-导出)。

设计：[Web 宿主与异步表面](../runtime/class-diagrams/web-host.md) · [Web 导出](../editor/class-diagrams/web-export.md) · [共享目标互斥](../editor/class-diagrams/asset-pipeline-system.md#目标切换互斥) · [Web 持久文件发布](../runtime/class-diagrams/web-storage.md)。核心公开类与项目 SDK 不新增；平台存储增加私有发布函数，影片呈现统一使用已有 RenderSubmitStatus 返回值。

## 引擎与系统所有权

Engine 管理根系统生命周期与帧调度，System 独占子系统；RuntimeHost 装配窗口、世界、渲染及可选服务。对象只由一个拥有者负责生命周期，其他关系为明确借用。

契约：[FrameLimiter](../runtime/class-diagrams/frame-limiter.md) · [运行宿主](../runtime/class-diagrams/runtime-host.md) · [引擎与系统](../runtime/class-diagrams/engine-system.md)。

## 字体与场景 UI

FontSystem 提供统一字形度量和绘制；UiSystem 借用世界、窗口及字体服务，UiComponent 绑定独立文档资源。控件不展开为 Node，业务 MVC 与场景跳转归项目。

契约：[字体系统](../runtime/class-diagrams/font-system.md) · [场景界面](../runtime/class-diagrams/ui-system.md)。

## 场景对象、组件及渲染引用

WorldSystem 拥有唯一 Flecs world；Node / Component 为带身份校验的值句柄。RenderSystem 在结构稳定阶段借用领域数据，GPU 对象由渲染私有资源拥有者管理。

契约：[场景](../runtime/class-diagrams/scene.md) · [节点](../runtime/class-diagrams/node.md) · [组件](../runtime/class-diagrams/component.md) · [渲染](../runtime/class-diagrams/render-system.md)。

## 物理与音频引用

世界拥有物理和音频系统。组件保存领域配置，系统管理第三方运行对象；编辑器编解码与 MCP 命令通过正式组件接口工作，不进入运行时。

契约：[物理系统](../runtime/class-diagrams/physics-system.md) · [物理组件](../runtime/class-diagrams/physics-components.md) · [音频系统](../runtime/class-diagrams/audio-system.md) · [音频组件](../runtime/class-diagrams/audio-components.md) · [音频导入](../editor/class-diagrams/audio-importer.md) · [组件命令](../editor/class-diagrams/component-commands.md) · [MCP 适配](../plugins/editor/class-diagrams/physics-audio-mcp-tools.md)。

## App 与日志

App 提供统一平台路径与宿主能力；LogSystem 管理日志生命周期。两者不包含项目策略，平台细节与第三方类型留在实现中。

契约：[App](../runtime/class-diagrams/app.md) · [日志系统](../runtime/class-diagrams/log-system.md)。

## 编辑器宿主拥有的独立系统

宿主先初始化 ProjectSystem；EditorEnvironment 借用项目，EditorSystem 借用环境。资源管线及构建/同步插件分别拥有制作和发布流程，Engine 不拥有项目系统。

契约：[项目](../editor/class-diagrams/project-system.md) · [编辑器](../editor/class-diagrams/editor-system.md) · [资源管线](../editor/class-diagrams/asset-pipeline-system.md) · [资源同步](../plugins/editor/class-diagrams/asset-sync-system.md)。

## 脚本运行、资源读取与项目入口

GameplaySession 组织项目启停；核心只声明 ScriptRuntime，宿主注入脚本工厂。插件内每个 Run 独占 VM 和绑定上下文；资源与存储仍经各自服务契约使用。

契约：[资源系统](../runtime/class-diagrams/resource-system.md) · [脚本入口](../plugins/runtime/class-diagrams/runtime-controls-script-api.md) · [原生 KV](../runtime/class-diagrams/key-value-store.md) · [脚本接口](../plugins/runtime/class-diagrams/storage-script-api.md) · [ScriptVm](../plugins/runtime/class-diagrams/script-vm.md) · [脚本系统](../plugins/runtime/class-diagrams/script-system.md) · [玩法入口](../runtime/class-diagrams/gameplay.md)。

## MCP 宿主与跨进程文件关系

MCP 协议与通道属于可选 runtime/mcp，编辑工具属于 editor/mcp；可执行宿主拥有端点，端点独占通道并借用普通运行时/编辑器接口。EditorCommands 在所属线程执行编辑操作，EditorSystem 不监听文件或持有协议回执。正式游戏排除运行端点和临时脚本执行；原生加载、状态与截图接口仍可使用。

[场景加载协调](../runtime/class-diagrams/scene-loading.md)、[首帧与截图](../runtime/class-diagrams/render-readiness.md)、[启动结果](../runtime/class-diagrams/gameplay-session.md)与[运行时 MCP](../plugins/runtime/class-diagrams/mcp-runtime-test.md)共用失败收尾和操作身份。命令回执是执行/受理事实，异步结果由对应原生请求保存。资源同步只发布完整版本，不替代宿主热重载流程。

契约：[协议与注册](../plugins/runtime/class-diagrams/mcp-system.md) · [项目工具](../plugins/editor/class-diagrams/project-mcp-tools.md) · [编辑器工具](../plugins/editor/class-diagrams/editor-mcp-tools.md) · [资源同步系统](../plugins/editor/class-diagrams/asset-sync-system.md)。

## 项目启停与编辑器命令

EditorSystem 调用 GameplaySession 组织资源、脚本与场景生命周期；命令负责其领域事务，停止、重开与恢复策略不下沉到核心资源接口。

契约：[运行会话](../runtime/class-diagrams/gameplay-session.md) · [编辑器](../editor/class-diagrams/editor-system.md)。

## 编辑器命令与场景业务

组件编辑使用所属编解码和 EditorCommands；字段编辑与整组件恢复保留各自冲突检查，不能因合并组织代码而扩大覆盖范围。

契约：[组件命令图](../editor/class-diagrams/component-commands.md) · [编辑器命令图](../editor/class-diagrams/editor-commands.md)。

## 公共路径工具

PathUtils 只提供 UTF-8 与原生路径转换；权限、目录创建、规范化和越界检查归实际调用者。

契约：[PathUtils](../runtime/class-diagrams/path-utils.md)。

## System 继承与参数依赖

具体系统继承 System；拥有者依赖先初始化、反序释放。可选服务使用可空借用或工厂，必需服务使用明确引用，不建立平行生命周期框架。

契约：[项目工具图](../plugins/editor/class-diagrams/project-mcp-tools.md) · [系统契约](../runtime/class-diagrams/engine-system.md)。

## 类型登记与框架分派

资源 Codec、AssetImporter、Component 操作及脚本绑定均由所属模块显式登记。框架只选择和调度，具体类型分别拥有转换、校验和状态，不集中到跨模块大 switch。

契约：[JsonImporter](../plugins/editor/class-diagrams/json-importer.md) · [JsonBinding / Json](../plugins/runtime/class-diagrams/json-script-api.md) · [ScriptRuntime](../runtime/class-diagrams/script-runtime.md) · [ScriptSystem](../plugins/runtime/class-diagrams/script-system.md) · [ScriptBindingRegistry](../plugins/runtime/class-diagrams/script-bindings.md) · [ComponentRegistry](../runtime/class-diagrams/component-registry.md) · [SceneComponentCodec](../editor/class-diagrams/scene-component-codecs.md) · [ResourceCodec](../runtime/class-diagrams/resource-codecs.md) · [AssetImporter](../editor/class-diagrams/asset-importers.md) · [ModelingMeshImporter](../plugins/editor/class-diagrams/modeling-mesh-importer.md)。

## 第三方资源拥有关系

第三方句柄由实际创建它的模块配对释放；GPU、音频、物理、VM 等对象不转移到通用 Manager。公开契约中的借用不代表共享所有权。

契约：[渲染](../runtime/class-diagrams/render-system.md) · [音频](../runtime/class-diagrams/audio-system.md) · [物理](../runtime/class-diagrams/physics-system.md) · [VM](../plugins/runtime/class-diagrams/script-vm.md)。

## 实际编译与链接依赖

依赖查找归 cmake/dependencies，插件目标归各自清单；宿主只装配启用的依赖闭包。实际边界由源码包含和解析后的目标依赖检查，不在本文再维护一份库清单。

契约：[绑定类图](../plugins/runtime/class-diagrams/script-bindings.md) · [导航绑定](../plugins/runtime/class-diagrams/navigation-script-api.md) · [导航系统类图](../runtime/class-diagrams/navigation-system.md) · [维护规范](../development.md) · [插件清单](../plugins/plugins.md)。

## 资源身份与导入依赖

核心目录维护 GUID / 逻辑路径与真实依赖；编辑器导入器维护源 meta、子资源身份和类型转换。源文件同属一个包不自动形成运行资源互相依赖。

契约：[资源目录](../runtime/class-diagrams/resource-catalog.md) · [GUID](../runtime/class-diagrams/guid-map.md) · [导入器](../editor/class-diagrams/asset-importers.md)。

## 资源调用关系

现行调用链为业务 → ResourceLoader → 资源图插件 → 核心文件读取与单项解析。插件拥有依赖加载、共享、计数和卸载；业务先加载再使用，未加载直接返回错误。

契约：[资源类图](index.md#资源系统) · [业务与任务](../runtime/class-diagrams/resource-consumers.md) · [统一生命周期](../runtime/class-diagrams/resource-lifecycle.md) · [加载契约](../runtime/class-diagrams/resource-loader.md)。

## 贴图与渲染基础

导入管线生成当前资源格式；运行时按准备预算创建 GPU 资源。材质绑定缓存、公共帧数据、逐观察者列表与阴影合批都收敛在渲染模块，业务不操作底层句柄。

契约：[贴图导入](../editor/class-diagrams/texture-importer.md) · [渲染](../runtime/class-diagrams/render-system.md) · [提交管线](../runtime/class-diagrams/render-submission.md)。

## 建模三层引用关系

建模插件内由网格算法、可编辑模型、命令与 MCP 适配组合制作流程；运行包只消费编译后的 Mesh，不链接完整建模插件。

契约：[建模](../plugins/editor/modeling.md) · [可编辑网格](../plugins/editor/class-diagrams/editable-mesh.md) · [建模命令](../plugins/editor/class-diagrams/modeling-commands.md)。

## 场景持久化与渲染编辑

场景持久模型来自 schema，保存/恢复在边界组装快照。环境、相机后处理和阴影各由所属组件保存；编辑器事务和 GPU 烘焙由编辑器模块组织。

契约：[场景持久化](../editor/class-diagrams/scene-persistence.md) · [渲染编辑命令](../editor/class-diagrams/render-commands.md) · [渲染 MCP](../plugins/editor/class-diagrams/render-mcp-tools.md) · [场景烘焙](../plugins/editor/class-diagrams/scene-bake.md)。

## 模型格式、统一运行数据与实例化

FBX / USD 由统一导入插件转换为独立 Model、Mesh、Skeleton、Animation、材质和纹理资源；运行时按真实引用实例化，不把离线解析器带入运行核心。

契约：[导入接口](../editor/class-diagrams/asset-importers.md) · [FBX](../plugins/editor/class-diagrams/fbx-importer.md) · [USD](../plugins/editor/class-diagrams/usd-import.md) · [模型资源](../runtime/class-diagrams/model-resources.md) · [解码器](../runtime/class-diagrams/resource-codecs.md) · [Animator](../runtime/class-diagrams/animator.md) · [动画系统](../runtime/class-diagrams/animation-system.md)。

## 角色控制器与编辑器扩展装配

角色插件包装核心运动能力，输入与相机插件由宿主登记；编辑器制作和 MCP 适配归编辑器插件。核心不判断项目玩法或控制器身份。

契约：[运行时角色插件](../plugins/runtime/class-diagrams/character-controller.md) · [核心 CharacterMotor](../runtime/class-diagrams/character-motor.md) · [编辑器角色插件](../plugins/editor/class-diagrams/character-controller.md)。

## 输入关系

InputSystem 收集设备并执行显式映射；项目配置绑定、玩家配对与虚拟触摸。原生输入与脚本接口共用同一状态来源。

契约：[输入系统](../runtime/class-diagrams/input-system.md) · [输入设计](../runtime/input.md)。

## 影片关系

宿主独占影片呈现服务，世界中的影片组件后期绑定；读取、解码、音频供数和呈现遵守各自任务与完成契约。专用 MCP、预览及异步导入的实现状态以所属图为准。

契约：[播放器](../runtime/class-diagrams/movie-player.md) · [启动装配](../runtime/class-diagrams/movie-startup.md) · [音频](../runtime/class-diagrams/movie-audio.md) · [导入](../plugins/editor/class-diagrams/movie-importer.md) · [任务](../editor/class-diagrams/asset-import-jobs.md) · [MCP](../plugins/editor/class-diagrams/movie-mcp-tools.md)。
