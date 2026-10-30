# 引擎类图

类图中的短类名按所在目录确定命名空间：核心为 `engine`，编辑器为 `engine::editor`，插件为 `engine::plugins::<插件名>`。核心不依赖编辑器或插件；编辑器库不依赖具体插件。插件通过底层接口提供扩展，由可执行宿主显式登记、装配；图中的借用接口不表示底层依赖其具体实现。

类图是架构评审入口，维护类的职责、所有权、公开方法和完整引用关系。构建与平台验证范围见根 [README.md](../../README.md)。

统一评审原则：**业务、运行宿主和编辑器都驱动最小核心，核心不承载调用方流程。** 评审每个方法时区分“提供能力”和“决定何时使用能力”，后者归调用方。完整边界见[调用方驱动核心](../architecture.md#调用方驱动核心)。

## 资源系统

各图分开维护核心单项能力、加载抽象接口、运行时资源依赖图插件以及编辑器构建插件。说明见[资源目录与同步加载](../runtime/resource-loading.md)。

| 独立类图                                                         | 评审内容                                      |
| ---------------------------------------------------------------- | --------------------------------------------- |
| [资源系统](../runtime/class-diagrams/resource-system.md) | 目录换代、单项读取解析及加载器借用 |
| [资源解码器](../runtime/class-diagrams/resource-codecs.md) | 逐资源类型解码、生成数据与声明依赖校验 |
| [加载接口](../runtime/class-diagrams/resource-loader.md) | 核心声明的 load/find/error 契约 |
| [资源依赖图插件](../plugins/runtime/class-diagrams/resource-graph.md) | 同代际共享、依赖闭包及反向释放 |
| [资源目录](../runtime/class-diagrams/resource-catalog.md)        | 只存元数据、GUID/路径索引、运行依赖与存储定位 |
| [已加载资源](../runtime/class-diagrams/resource.md) | 单项独立载荷、目录代际及只读数据借用 |
| [虚拟文件访问](../runtime/class-diagrams/virtual-file-system.md) | SDL3 散文件与包内范围读取，不认识编辑器业务   |
| [编辑器资源构建](../plugins/editor/class-diagrams/asset-build-system.md) | 项目打包清单、增量输出和精简运行清单          |

核心不扫描、监听、导入或打包，也不区分编辑器模式。编辑器扫描全部 assets，直接更新核心目录；真机启动提交固定精简清单一次。两端共用加载接口和选中的资源依赖图插件，编辑器无需打包即可运行。跨层调用见[资源调用关系](references.md#资源调用关系)。

## 当前宿主与导入模块

- [运行宿主](../runtime/class-diagrams/runtime-host.md)：装配具体系统，处理窗口事件、暂停与帧提交。
- [脚本运行契约](../runtime/class-diagrams/script-runtime.md)：核心定义的执行接口与状态，宿主通过工厂选择插件。
- [玩法运行会话](../runtime/class-diagrams/gameplay-session.md)：协调脚本、场景、GPU 与项目资源的启动和释放。
- [核心 GUID 映射](../runtime/class-diagrams/guid-map.md)：编辑器与真机复用，完整 GUID 与逻辑路径双向查询。

- [任务系统](../editor/class-diagrams/task-system.md)：Taskflow 执行器，项目默认 4 个导入线程。
- [逐类型资源导入](../editor/class-diagrams/asset-importers.md)：通用导入契约、显式登记与每类资源独立转换。
- [FBX 模型资源导入](../plugins/editor/class-diagrams/fbx-importer.md)：ufbx 解析、稳定子资源、骨骼与动画采样。
- [USD 静态模型导入](../plugins/editor/class-diagrams/usd-import.md)：独立源格式插件、自包含静态多边形及增量子网格。
- [字体资源导入](../plugins/editor/class-diagrams/font-importer.md)：独立编辑器插件读取 TTF 和 meta，生成共享字形曲线与度量资源。
- [UI 资源导入](../plugins/editor/class-diagrams/ui-importer.md)：文档、字体和图片引用转换为标准运行资源依赖。
- [建模资源导入插件](../plugins/editor/class-diagrams/modeling-mesh-importer.md)：宿主注入网格导入扩展，编辑器管线不依赖建模插件。
- [场景组件描述编解码](../editor/class-diagrams/scene-component-codecs.md)：组件 TOML 读取、验证和写出按类型拆分。
- [场景导入器](../editor/class-diagrams/scene-importer.md)：TOML 场景读写与生成数据校验。
- [音频导入器](../editor/class-diagrams/audio-importer.md)：按 meta 转换 WAV 为交错 PCM，生成 GUID 资源。
- [资源管线](../editor/class-diagrams/asset-pipeline-system.md)：meta 身份、SQLite 索引、依赖增量和 GUID 缓存发布。

## 场景与项目运行

- 场景中的全部对象必须通过 Node + Component 表达，包含相机与灯光；场景通过 GUID 组装独立 Mesh、材质与 Shader 资源。
- [场景](../runtime/class-diagrams/scene.md)：世界独占活动场景，场景独占节点。
- [节点](../runtime/class-diagrams/node.md)：轻量身份、层级及无具体类型的组件列表；泛型添加和查询。
- [组件创建登记](../runtime/class-diagrams/component-registry.md)：按生成类型登记工厂，节点泛型创建，组件自行恢复数据。
- [组件](../runtime/class-diagrams/component.md)：Component、Transform、Camera、Light、MeshRenderer 及当前参数。
- [模型资源](../runtime/class-diagrams/model-resources.md)：模型节点层级、骨架/动画与子资源 GUID，核心读取统一生成数据。
- [动画组件](../runtime/class-diagrams/animator.md)：片段选择、播放、暂停、定位及运行状态。
- [角色物理组件](../runtime/class-diagrams/character-motor.md)：胶囊运动、地面/台阶与 Jolt 查询，不解释输入。
- [角色控制器](../plugins/runtime/class-diagrams/character-controller.md)：可选运行时输入与第一/第三人称相机策略。
- [物理组件](../runtime/class-diagrams/physics-components.md)：Collider、RigidBody、碰撞形状、运动设置与瞬态冲量。
- [音频组件](../runtime/class-diagrams/audio-components.md)：AudioSource、AudioListener、共享音频资源与播放状态。
- [字体系统与 3D 文字](../runtime/class-diagrams/font-system.md)：核心 FontSystem 共享曲线资源、文字排布和 GPU 绘制，普通节点挂载 TextComponent。
- [场景界面](../runtime/class-diagrams/ui-system.md)：普通节点挂载 UiComponent，RmlUi 文档内部控件不展开为场景节点。
- [材质](../runtime/class-diagrams/material.md)：内置/项目路径选择材质与 Shader，按名字和数值类型匹配参数。
- [场景数据模型](../runtime/class-diagrams/scene-data.md)：FlatBuffers 定义、C++/TS 生成及组件数据所有权。
- [编辑器环境](../editor/class-diagrams/editor-environment.md)：平台路径映射和编辑器目录提交。
- [渲染](../runtime/class-diagrams/render-system.md)：同一窗口的空白/PBR 场景呈现和每次运行 GPU 资源。
- [编辑器](../editor/class-diagrams/editor-system.md)：默认待机，主线程处理 MCP 启停，停止恢复空白。
- [场景预览](../editor/class-diagrams/scene-preview.md)：独立隐藏镜头、自由飞行、增量导入及未保存编辑保护；不推进玩法系统。
- [脚本公开接口](../runtime/class-diagrams/gameplay.md)：Gameplay 加载场景或动态创建节点与组件，运行停止或重载后旧引用安全失效。

- [绑定接口与注册表](../plugins/runtime/class-diagrams/script-bindings.md)：逐类实现 ScriptBinding，新增绑定只需增加类和注册项。

- [网格工具](../runtime/class-diagrams/mesh-utils.md)：验证几何及按上游算法生成切线。
- [贴图导入](../editor/class-diagrams/texture-importer.md)：按 meta 配置调用 BasisU 生成 GUID 资源。

## 脚本接口

- [App 平台路径](../runtime/class-diagrams/app.md)：缓存、包内资源和持久数据具有跨平台一致含义。
- [日志系统](../runtime/class-diagrams/log-system.md)：宿主管理的单例，默认写入磁盘。
- [项目玩法入口](../runtime/class-diagrams/gameplay.md)：项目 `Game` 继承 `Gameplay`，可承载项目业务与场景流转。
- [资源同步](../plugins/editor/class-diagrams/asset-sync-system.md)：完整批次发布到持久资源版本目录。

- [渲染脚本接口](../plugins/runtime/class-diagrams/render-script-api.md)：环境色、曝光、阴影、清屏色，以及投影、灯光、具名贴图和材质槽。
- [动画脚本接口](../plugins/runtime/class-diagrams/animation-script-api.md)：逐类 Animator 绑定及播放状态。
- [物理与音频脚本接口](../plugins/runtime/class-diagrams/physics-audio-script-api.md)：组件创建、设置、冲量与播放控制，复用原生能力。

## MCP 与编辑器命令

- [建模职责边界](../plugins/editor/modeling.md)：独立 C++ 几何核心、编辑器业务与 MCP 适配。
- [可编辑网格](../plugins/editor/class-diagrams/editable-mesh.md)：生成模型、稳定身份、校验与渲染网格编译。
- [基本体](../plugins/editor/class-diagrams/mesh-primitives.md)、[拓扑运算](../plugins/editor/class-diagrams/mesh-topology.md)、[几何与 UV](../plugins/editor/class-diagrams/mesh-geometry.md)：无项目和编辑器依赖的计算接口。
- [建模编辑业务](../plugins/editor/class-diagrams/modeling-commands.md)：节点关联、选择、事务与资源保存。
- [建模 MCP](../plugins/editor/class-diagrams/modeling-mcp-tools.md)：工具注册及命令转发。

- [MCP 协议与注册](../plugins/editor/class-diagrams/mcp-system.md)：McpSystem 只管理协议、工具描述和执行函数，名称与参数模型来自同一注册项。
- [项目 MCP 工具](../plugins/editor/class-diagrams/project-mcp-tools.md)：ProjectMcpTools 显式配置项目与本机/Android 目标，按业务注册构建、同步、运行调试和启停工具。
- [编辑器 MCP 工具](../plugins/editor/class-diagrams/editor-mcp-tools.md)：EditorMcpTools 注册场景查询、组件属性编辑、批量、撤销/重做及回执查询，通过文件通道交给编辑器执行。
- [场景预览 MCP](../plugins/editor/class-diagrams/scene-preview-mcp-tools.md)：独立登记打开、关闭、查询、镜头设置及资源刷新工具，转发给编辑器预览模块。
- [编辑器命令与撤销](../editor/class-diagrams/editor-commands.md)：EditorCommands 调度模块命令并拥有撤销历史，SceneCommands 借用世界并注册场景业务；EditorSystem 在主线程消费命令。
- [模型实例化命令](../editor/class-diagrams/model-commands.md)：展开完整模型节点层级、资源版本快照及撤销/重做。
- [角色控制器编辑器插件](../plugins/editor/class-diagrams/character-controller.md)：类型自己的 TOML、编辑命令与 MCP 注册。
- [按类型组件命令](../editor/class-diagrams/component-commands.md)：逐类型注册，ComponentEdit 管理添加、属性快照、回滚与撤销。
- [物理与音频 MCP](../plugins/editor/class-diagrams/physics-audio-mcp-tools.md)：参数 schema、工具登记与主线程命令转发。

MCP 适配器和编辑器业务分别注册各自接口。Node、Component 与引擎核心不引用 MCP、JSON 协议或撤销框架；工具进程不持有编辑器进程中的场景指针。跨进程身份、完成回执与引用方向见[完整引用关系](references.md#编辑器命令与场景业务)。

## 插件清单

[插件目录与构建登记](../plugins/plugins.md)维护运行时/编辑器插件范围。每个插件对应一份 CMake 描述，注册器按显式选择及依赖闭包创建目标；未选插件不参与宿主装配。

## 阅读入口

- [引擎与系统总图](../runtime/class-diagrams/engine-system.md)：只定义 `Engine` 和抽象基类 `System`。
- [完整引用关系](references.md)：分节列出全部继承、拥有、借用、参数、返回类型与第三方依赖。
- [配置与状态](../runtime/class-diagrams/types.md)：`EngineConfig` 和 `FrameStatus`。
- [公共路径工具](../runtime/class-diagrams/path-utils.md)：`PathUtils` 只负责 UTF-8 与文件系统路径转换。

模块说明和独立类图跟随源码职责，插件进一步区分运行时和编辑器；本页与完整引用关系保留为跨层统一入口：

| 范围   | 文档与独立类图                                  | 源码职责                                          |
| ------ | ----------------------------------------------- | ------------------------------------------------- |
| 运行时 | `docs/runtime/`、`docs/runtime/class-diagrams/` | 引擎、系统、App、日志、资源、世界子系统及公共工具 |
| 编辑器基础 | `docs/editor/`、`docs/editor/class-diagrams/` | 项目环境、资源管线、Task 与通用主线程命令 |
| 运行时插件 | `docs/plugins/runtime/`、对应 `class-diagrams/` | 资源依赖图、脚本虚拟机与逐类绑定、可选角色控制器 |
| 编辑器插件 | `docs/plugins/editor/`、对应 `class-diagrams/` | 完整建模、完整 MCP、资源构建与同步、模型、字体与 UI 导入 |

头文件和实现对应 `include/engine/<scope>/` 与 `src/engine/<scope>/`。核心不引用具体插件，运行时插件不反向依赖编辑器；TS 公开声明集中在 [include/engine.d.ts](../../include/engine.d.ts)。

## 运行时根系统

RuntimeHost 独占窗口，资源底层可由可执行宿主先初始化并借用，或由 RuntimeHost 默认装入 Engine；世界和渲染仍归 Engine。可执行宿主装配选中的资源依赖图插件，并通过 ScriptRuntime 工厂创建脚本插件；RuntimeHost 库不引用或链接具体插件。

| 初始化顺序 | 独立类图                                                 | 所有者及职责                                             |
| ---------- | -------------------------------------------------------- | -------------------------------------------------------- |
| 1          | [窗口系统](../runtime/class-diagrams/window-system.md)   | RuntimeHost 独占；SDL3 窗口和事件                        |
| 2 | [资源系统](../runtime/class-diagrams/resource-system.md) | 外部宿主或 Engine 独占；SDL3、单项解析、目录代际 |
| 3          | [世界系统](../runtime/class-diagrams/world-system.md)    | Engine 独占；flecs 世界、场景和子系统生命周期            |
| 4          | [渲染系统](../runtime/class-diagrams/render-system.md)   | Engine 独占；bgfx 设备、空白帧和资源网格 PBR             |

## 世界子系统

| 顺序 | 独立类图                                                   | 当前依赖       |
| ---- | ---------------------------------------------------------- | -------------- |
| 1    | [物理系统](../runtime/class-diagrams/physics-system.md)    | Jolt           |
| 2    | [导航系统](../runtime/class-diagrams/navigation-system.md) | Recast、Detour |
| 3    | [动画系统](../runtime/class-diagrams/animation-system.md)  | ozz-animation  |
| 4    | [字体系统](../runtime/class-diagrams/font-system.md) | 核心字体资源与 bgfx 曲线绘制 |
| 5    | [界面系统](../runtime/class-diagrams/ui-system.md)         | RmlUi；借用核心 FontSystem |
| 6    | [音频系统](../runtime/class-diagrams/audio-system.md) | SDL3 Audio |
| 7    | [脚本系统](../plugins/runtime/class-diagrams/script-system.md)     | QuickJS/V8 编译期二选一；宿主通过核心工厂装配 |

[虚拟机中间层](../plugins/runtime/class-diagrams/script-vm.md)维护单后端构建和每 Run 环境；[逐类绑定](../plugins/runtime/class-diagrams/script-bindings.md)在两种后端间共用，不向核心增加虚拟机类型。

## 宿主模块

- [Android 宿主](../runtime/class-diagrams/android-host.md)：SDL3 Activity 加载原生引擎，Activity 与 Surface 在销毁前同步暂停渲染；不要求编辑器项目。

- [项目系统](../editor/class-diagrams/project-system.md)：编辑器宿主先检查项目根目录及 settings、cache、assets；接入 toml++。
- [编辑器系统](../editor/class-diagrams/editor-system.md)：借用已就绪的编辑器环境，创建 Dear ImGui 上下文，支持编辑器子系统。
- `engine_app` 使用默认配置即可启动；`editor_app` 必须传入 `--project <path>`，先验证项目再启动窗口。编辑器默认空白待机，MCP 启动后项目占满同一窗口，停止后恢复空白；尚未绘制编辑器业务界面。
- [资源管线系统](../editor/class-diagrams/asset-pipeline-system.md)：Taskflow、SQLite、efsw、meshoptimizer；由资源工具宿主单独管理。
- shaderc、texturec、geometryc 是构建提供的宿主工具；fmt 用于基础格式化。它们不引入额外引擎包装类。
- 运行时不反向依赖编辑器、离线资源管线或具体游戏。Android 打包参考 SDL3 自带 Android 工程；APK 内置资源通过 SDL 文件流读取，平台验证范围见根 [README.md](../../README.md)。

## 维护约定

- 遵守根 [AGENTS.md](../../AGENTS.md) 的 C++20 规范；不使用异常，通过返回值表达状态。
- 用户评审类、职责、所有权、关系和公开方法；助手负责实现、测试和图与代码同步。
- 每个类的方法只在所属图中定义一次。其他图出现同名类时仅表示外部引用，不重复维护接口。设计先于实现时明确标注状态，不能将设计方法写成已经可调用的功能。
- 每个具体系统及子系统独立成图。共有生命周期只在 `System` 中维护；自有头文件以 `runtime/`、`editor/`、`plugins/runtime/`、`plugins/editor/` 开头，构建提供统一搜索根目录。
- `+`、`#`、`-` 分别表示公开、受保护、私有；`<|--` 表示继承，`*--` 表示独占拥有，`o--` 表示持有共享底层资源的原生引用，`-->` 表示非拥有引用，`..>` 表示使用依赖。
- 方法采用 Mermaid 的“方法(参数) 返回类型”语法；静态方法用 `$`、纯虚方法用 `*`，C++ 限定符在图下注明。指针返回值的星号写为 `#42;`，避免被 Mermaid 误认成纯虚标记后省略。
- 关系标签使用普通文本；带 `::` 的 C++ 类型写在成员声明或图外说明中，避免额外冒号触发解析错误。修改后检查 Mermaid 解析和渲染。
- `library` 是第三方库，`external` 是第三方资源类型；标准库类型在方法签名中表达。拥有资源不等于拥有库本身。
- 已选依赖必须实际编译和链接；依赖接入检查不代表对应模块全部能力已经实现，实际范围以各模块说明和验证记录为准。

- [渲染编辑命令](../editor/class-diagrams/render-commands.md)：环境光、阴影、曝光与真实帧截图。
- [渲染 MCP 适配](../plugins/editor/class-diagrams/render-mcp-tools.md)：效果控制、异步截图请求和状态。
- [场景持久化](../editor/class-diagrams/scene-persistence.md)：模型、材质、灯光、相机和层级的完整源资源保存。

- [UI 脚本与 MVC](../plugins/runtime/class-diagrams/ui-script-api.md)：Model、View、Controller 的职责，控件绑定与事件订阅生命周期。
