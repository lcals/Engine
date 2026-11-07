# 引擎架构总览

架构评审从[类图索引](class-diagrams/index.md)进入：总图只定义 `Engine` 与 `System`，各领域类独立维护职责、所有权、依赖和公开方法。类图与代码同步，模块使用说明从[首页清单](../README.md#功能与模块)进入，实际验证范围见[测试与范例](testing.md)。

[资源核心](runtime/resource-loading.md)提供目录、GUID/VFS 与单项加载底层；运行时资源依赖图插件实现依赖树加载、共享缓存和反向卸载。编辑器通过基础管线与构建/同步插件组织导入和发布，真机启动使用固定精简清单。目录不持有全部实体数据。

## 单一版本维护

项目只维护当前一个版本，不考虑历史版本兼容，不编写或保留兼容代码。该约定覆盖 C++ 与脚本接口、编辑器命令与 MCP、配置与资源格式、命名空间、目录及构建目标；插件替代实现同样遵守当前契约。

契约变化先更新所属类图供评审，确认后一次更新实现、全部调用方、绑定、工具、测试与文档。仅为旧版本保留的接口、别名、转发壳、重载、字段、格式转换及双轨分支一并删除；不设置弃用过渡期，也不让业务选择新旧两套入口。历史类图中的兼容保留说明属于待清理项，不作为继续增加或保留兼容代码的依据。

项目源数据随当前格式同步升级，缓存、生成代码和运行资源包按当前契约重新生成；运行时遇到不匹配的格式明确报错，不猜测格式、不兼容读取，也不执行历史格式转换。格式标识可用于检测并拒绝过期数据。资源内容更新产生的代际、仍被引用的资源快照、失效句柄校验及原子发布属于当前版本的生命周期保护，仍按所属模块契约维护。

## 调用方驱动核心

这是整个架构的设计原则：玩法业务、运行宿主和编辑器都调用同一套最小核心能力，按各自需要组织流程。核心不决定谁在调用、什么时候应该调用或调用几次，只完成明确请求并返回状态。

| 角色     | 决定什么                                     | 如何使用核心                                   |
| -------- | -------------------------------------------- | ---------------------------------------------- |
| 玩法业务 | 场景选择、对象创建、业务行为、资源使用时机   | 调用场景、节点组件、资源和脚本等模块的通用接口 |
| 编辑器   | 项目启停、扫描导入、编辑操作、打包和调试流程 | 调用相同核心接口，组合编辑器自己的业务         |
| 运行宿主 | 平台入口、帧推进、部署来源和启动顺序         | 装配系统，调用核心初始化、更新与关闭           |
| 核心     | 每项能力内部的正确执行、状态与生命周期约束   | 提供最小接口，返回成功、失败或必要状态         |

### 组件与独立服务的边界

以状态归属和生命周期决定接口形态，组件不是所有引擎能力的统一包装。

| 归属 | 项目接口 | 例子与边界 |
| --- | --- | --- |
| 场景或对象的配置、资源关联和行为意图 | 获取组件后调整属性；需要保存的值进入生成 ECS Data | 环境/大气、相机后处理、灯光、网格 LOD/实例化、音源、角色和导航代理；材质通过 MeshRenderer 的材质槽句柄访问 |
| 宿主、设备或进程级的独立服务 | 按所属服务直接访问，不挂场景节点 | Window 的尺寸/模式/鼠标、Render 的 VSync/MSAA/各向异性、时间/帧率、输入、资源和存储；不得在这里增加另一份场景效果配置 |
| 无持久场景配置的查询与显式操作 | 保留所属领域的直接接口 | 物理射线/范围查询、导航路径查询与显式构建请求；有状态的对象仍由组件表达，不为一次查询创建占位组件 |
| 编辑、构建和诊断 | 编辑器命令与 MCP | 导入、烘焙、线框、包围盒、统计与调试线；临时诊断状态不进入场景/预制体 |

场景组件应有明确归属，能随对象组合并保持保存、恢复和调试语义。窗口与显示设备不属于某个场景，不创建 WindowComponent 来包住同一个全局窗口。相反，环境和后处理属于场景画面，不能通过一个全局 Render setter 绕开其组件状态。

优先扩展既有组件，不为每项效果新建组件。组件上的可选效果开关默认关闭，由项目显式开启；组件本身的基础 enabled/visible 与这一规则不同。缺少或禁用场景组件使用固定默认值，切场景不继承前一场景参数。

资源加载时自动处理必要依赖由运行时资源依赖图插件实现；是否加载哪个场景、是否启用热重载、何时应用新目录属于调用方决策。窗口、世界、渲染、脚本等模块同样遵循这一划分，不把上层流程塞进 Engine 或任何核心系统。

“编辑器多次更新目录，真机启动提交一次”只是调用频率的例子，不是核心里的两种模式。编辑器与运行时不需要两套资源机制，也不为调用方另建通用 Driver 类。具体所有权和公开接口以独立类图为准。

## 分层与依赖

| 代码范围 | 命名空间 | 允许依赖 |
| --- | --- | --- |
| 运行时核心 | `engine` | 标准库、其他运行时核心模块、该系统必需的第三方实现 |
| 编辑器基础 | `engine::editor` | 运行时核心、其他编辑器基础模块、离线工具依赖 |
| 运行时插件 | `engine::plugins::<插件名>` | 运行时核心及清单声明的运行时插件 |
| 编辑器插件 | `engine::plugins::<插件名>` | 核心、编辑器基础及清单声明的插件 |
| 可执行宿主 | 入口文件的局部作用域 | 装配本次构建选择的核心、编辑器与插件 |

依赖方向按头文件与实际链接共同判断。核心不能引用编辑器或具体插件，编辑器库不能引用具体插件；插件之间也不得形成环。编辑器需要插件能力时，由编辑器定义最小接口，插件实现，宿主显式注册。编辑器调用接口不改变编译依赖方向。入口文件不属于编辑器基础库，负责选择实现和管理装配生命周期。

Engine/System 框架只依赖标准库。SDL、bgfx 等必要的系统实现依赖保持目标级私有配置；公开头优先前置声明借用类型。FlatBuffers 是既定跨语言模型，公开契约需要时保留生成类型，不为隐藏它额外复制数据结构。移动命名空间时同步前置声明、调用点和类图，不通过根命名空间兼容别名保留旧边界。

公开头文件和实现分别位于 `include/engine/`、`src/engine/`，采用相同分层：

- `runtime/`：系统框架、运行时宿主、玩法会话、脚本执行契约、平台路径、窗口、日志、资源、世界、渲染、物理与音频；公共工具位于 `runtime/utils/`。
- `editor/`：编辑器环境、项目约束、通用命令、编辑器任务域使用和资源导入基础；线程与任务系统仍属于运行时核心。
- `plugins/runtime/`：可独立选择的资源依赖图、脚本执行、编译期可选 QuickJS/V8 后端与逐类绑定实现。
- `plugins/editor/`：完整建模、统一源格式导入、MCP 协议及业务适配、资源构建和资源同步插件。

运行时插件调用核心接口，编辑器插件可以组合运行时插件及编辑器基础。核心通过自己定义的接口接收宿主注入的实现，不引用具体插件；运行时插件不得反向依赖编辑器或具体项目。平台专用实现、入口与打包资源集中于 `platforms/<platform>/`。自有头文件从 `runtime/`、`editor/`、`plugins/runtime/` 或 `plugins/editor/` 开始包含；CMake 目标传递搜索根，不使用相对目录回跳。

平台专用编辑器基础实现位于 `platforms/<platform>/editor/`，具体编辑器插件的实现位于其中的 `plugins/<name>/`，继续由所属插件目标编译。比如 Android adb 同步由桌面 MCP 执行，源码位于 `platforms/android/editor/plugins/mcp/`，不进入 APK 或运行时核心。`platforms/desktop/app.cpp` 与 Android 的 `native/app.cpp` 实现核心 App，并非可自由装配插件的宿主；真正的可执行装配入口与独立测试入口按其实际职责判断。

模块说明位于 `docs/runtime/`、`docs/editor/`、`docs/plugins/runtime/` 和 `docs/plugins/editor/`，独立类图位于各范围的 `class-diagrams/`；跨层入口和[完整引用关系](class-diagrams/references.md)位于 `docs/class-diagrams/`。TypeScript 公共声明统一维护在 `include/engine.d.ts`，只暴露已实现绑定。

## 构建边界

### 插件可替换原则

所有插件都是某项能力的实现，原则上允许其他人提供遵守同一契约的版本。契约包括公开方法、数据、状态与错误、所有权、同步/异步完成语义和生命周期；替代实现不能只保持方法名称而改变这些行为。宿主负责选择、登记和注入，上层面向能力契约，不依赖默认实现或通过向下转型取得其内部对象。

实现方式按职责选择，不要求所有插件都继承统一虚基类。**组合包装**用于在底层能力上组织高层行为：资源插件包装核心文件/单项资源读取，增加依赖加载、共享、计数和卸载；业务只使用高层加载接口。**底层接入与注入**用于接入不同后端，例如通过 ScriptRuntime 接入脚本运行实现；逐类型扩展也可采用已有登记契约。**独立功能**自成模块，由业务通过公开契约调用，不要求包装既有底层能力或实现统一基类。这三种是主要职责形态，可以按需要组合；包装对象也可以实现用于替换注入的接口，不额外建立通用 Manager。

该原则自本轮确认起约束后续设计，不表示已有全部插件已经完成替换验证。资源的目标类图见[统一生命周期](runtime/class-diagrams/resource-lifecycle.md)。这里的替换是符合契约的实现选择与装配，不承诺运行时热插拔、跨版本 C++ ABI 或自动迁移运行状态。

### 目标与装配

CMake 分为安装、依赖、插件、核心、编辑器与平台六类目录。每个插件在 `cmake/plugins/<scope>/<name>.cmake` 自声明，登记器按 `ENGINE_RUNTIME_PLUGINS` / `ENGINE_EDITOR_PLUGINS` 选择并解析依赖闭包；未选插件不创建目标，宿主只装配启用实现。目录归属、依赖方向和目标选择共同约束边界，不把移动文件名当作完成插件化。

资源加载契约由核心 ResourceLoader 定义，默认 ResourceGraphSystem 插件组合底层能力。当前源码仍有 ResourceSystem 单项加载器回退及公开共享指针；[统一资源目标](runtime/class-diagrams/resource-lifecycle.md)将单项读取与高层契约分离，由插件实现依赖加载、共享计数与卸载，业务只使用已注入契约。该目标尚未实现。宿主先初始化核心，再启用插件，关闭时先停止使用者、解除借用，再释放插件和核心。

脚本执行契约由核心 [ScriptRuntime](runtime/class-diagrams/script-runtime.md) 定义，状态使用普通 ScriptStatus。可执行宿主按插件清单向 RuntimeHost 提供创建函数，取得实现该契约的 ScriptSystem 并交给 WorldSystem 独占调度。RuntimeHost 和 GameplaySession 不包含或链接具体脚本插件；未选择插件仍可提交资源目录和运行原生场景，脚本玩法启动明确返回不可用。脚本插件内部以 ScriptVm 隔离 VM，`ENGINE_SCRIPT_BACKEND` 默认 `quickjs`，也可在构建时选择 `v8`；互斥宏仅编译和链接所选后端，没有运行时切换。每个 Run 独占 VM 环境、模块、Gameplay 与任务队列，共用逐类绑定、UI MVC 和热重载流程。独立 V8 依赖验证不向核心装配脚本伪系统。

## 框架与领域职责

`Engine` 只管理抽象根系统的注册、初始化、更新和反序关闭。`System` 独占子系统，统一管理递归生命周期。具体系统装配、窗口事件、暂停与帧调度归 `RuntimeHost`，Gameplay 启停、部署目录提交与场景、GPU 资源的释放归 `GameplaySession`；CPU 资源按实际共享引用回收。脚本状态、场景查询和渲染统计由各自模块提供，不向 Engine 增加便利转发。

运行时启动不要求项目目录。编辑器通过独立 `ProjectSystem` 校验项目，再由 `EditorEnvironment` 准备路径及资源；运行时消费部署后的 catalog 与 GUID 二进制。Android 宿主读取包内或持久版本，不依赖编辑器项目结构。

场景真实数据唯一存于 WorldSystem 的 Flecs world：生成的节点属性与具体组件按 ECS 列存放，ChildOf 是唯一层级；有效激活/可见性和世界矩阵是安全点刷新缓存。Node、Component 与 Material 是可复制身份值句柄，Scene 管理发布、卸载、资源与局部 ID 索引，不拥有节点或组件行为对象。各 System 直接查询 Published 实体的相关列；保存、撤销和编辑枚举才组装独立快照。组件/资源模型继续由 FlatBuffers schema 定义；GUID 与逻辑路径的双向映射位于核心模块，桌面与设备共用。资源管线负责源文件、meta、SQLite 与转换；构建和同步插件组织发布。资源核心读取单项并解析，依赖图插件加载必要依赖并维持共享使用关系。

物理与音频同样分层：`Collider`、`RigidBody`、`AudioSource`、`AudioListener` 以及 `PhysicsSystem`、`AudioSystem` 属于运行时核心。Jolt 物体、SDL 输出设备与流由对应系统管理，公开组件值句柄暴露生成设置快照和普通 C++ 请求，真实设置/意图存于 ECS 列。WAV/meta 转 PCM、场景 TOML 读写、组件快照与撤销属于编辑器。MCP 插件只注册工具并转发编辑器命令；脚本插件逐类绑定核心接口。两类插件都不复制模拟或播放逻辑，核心也不读取 JSON 命令与编辑器设置。

MCP 协议层只登记、校验并调用工具。项目工具和场景工具各自注册；场景操作通过命令通道交给编辑器主线程，编辑器命令模块组织批量执行、回滚与撤销，场景模块调用普通组件接口。组件和 Engine 不知道 MCP、命令历史或编辑器项目。新业务模块分别登记需要暴露的工具和执行函数，所有权与线程边界保持显式。

实现采用 C++20 与返回值错误处理，资源所有权和失败清理保持明确。依赖使用固定版本的 vcpkg port 或 Git 子模块，实际参与系统编译和链接。代码规则见 [AGENTS.md](../AGENTS.md)，构建和补丁维护见[开发说明](development.md)。

## 全模块架构审查（2026-11-04）

本轮覆盖全部运行时核心、编辑器基础、运行时/编辑器插件、平台实现、CMake 及测试/工具目录；原有 128 份类图文档全部参与核对，并补充审查统一导入插件关系图，当前共 129 份。检查包括类和公开方法对应、所有权、数据唯一性、直接包含、实际目标链接、模块必要性与内部实现归属。算法正确性、完整平台功能和每个函数的行为不由静态结构审查代替，实际验证单独记录在[测试文档](testing.md)。

### 职责与收敛判断

| 范围 | 当前边界与审查判断 |
| --- | --- |
| Engine / System / RuntimeHost / GameplaySession | 框架管理系统，宿主装配与逐帧调度，会话处理部署/启停；职责不同，保留，不再加便利转发或 Driver |
| App / Window / Input / Storage / Log | 独立平台和进程服务；平台实现与业务分离，保留直接接口，不组件化窗口 |
| ThreadSystem / TaskSystem | 共享并行预算与独立任务域不同，保留两层；领域系统借用任务域，不另建线程池 |
| World / Scene / Node / Component / Prefab | 世界拥有唯一 ECS；Scene 管场景发布和局部身份索引；值句柄访问数据，预制体记录模板关系与覆盖，保留现有结构 |
| Resource / Catalog / VFS / Codec / ResourceGraph | 目录、读取、单项解析与依赖树/共享引用分别有职责；依赖图继续留可选插件，不并回资源核心 |
| Render / GPU Resources / Passes | 系统组织视图，资源类负责 GPU 所有权，各 pass 实现对应算法；帧快照是派生输入，不是第二份场景配置 |
| Physics / Collider / RigidBody / CharacterMotor | 系统拥有 Jolt 运行对象，组件表达设置和控制意图；碰撞查询作为领域服务，不能因代码量大就搬进项目或拆出平行模拟框架 |
| Navigation / NavigationCrowd / NavigationAgent | 核心提供 Recast/Detour 能力，插件把代理与 ECS 联动；路径查询无需包装成组件，代理对象需要组件 |
| Animation / Animator / CharacterController | 骨架采样、实例状态与玩家控制策略各有归属；控制器/相机/可见性策略留角色插件，保留组合 |
| UiSystem / FontSystem / TextComponent | 文档布局/输入与通用字体排布/GPU 绘制分开，3D 文字复用字体核心；不合并，RmlUi 适配私有，离线字体解析留导入插件 |
| ScriptSystem / ScriptVm / Bindings / UI MVC | 运行生命周期、VM 后端隔离、逐类外观、项目业务四层各有必要；后端构建互斥，不新加通用 Manager |
| Terrain / Destruction / Effects | 运行时体积修改、Blast/Jolt 碎块与实时特效属于插件；离线破碎准备、导入和编辑事务继续留编辑器插件 |
| Project / EditorEnvironment / EditorSystem | 项目目录检查、路径/目录应用与编辑器生命周期分工成立；编辑器借用运行时能力，不能反向让核心读取项目 TOML |
| AssetPipeline / Importers / SceneComponentCodec | 管线组织批次/依赖/原子发布，各类型拥有识别和转换；TOML 编解码留编辑器，不搬进运行时组件 |
| AssetImport / Modeling / Editor Mesh | FBX/USD/字体/UI/JSON 的格式导入收敛到统一 `asset_import` 插件，各类型保留独立实现及宿主登记；建模制作留原插件，MikkTSpace 与 LOD 共用 `editor/mesh` 内部目标 |
| SceneBake / AssetBuild / AssetSync / MCP / UI Editor | 烘焙、出包、同步、协议与 UI 编辑分别有生命周期；MCP 只调用真实领域命令，平台传输实现从通用目录分离 |
| Tests / Showcase Tools / Build Scripts | 测试按模块归档；公开项目资产生成器归项目 tools，根 scripts 保留可复用工程工具；图与说明不重复维护实际测试结果 |

保留核心/编辑器/插件分层；五个仅承担格式导入的插件已收敛为一个 `asset_import`，不新增类或注册框架，详见[统一导入边界](plugins/editor/class-diagrams/asset-import.md)。制作、模拟和编辑命令各有职责，继续独立。其余重复主要是文档重复定义、项目工具混在通用目录，以及个别内部辅助暴露为公共头，已按职责修正。

### 本次职责收敛

- `scene_commands.cpp` 保留节点生命周期、活动相机选择、身份单元与显式登记。Transform、Camera、Light 和材质编辑归 `commands/components/` 的类型文件；诊断快照归 `commands/scene_query.cpp`。原有 EditorCommands、EditorEdit、NodeAliases 继续负责事务、回滚与创建后重做身份，命令和 JSON 契约不变。
- 经类图评审后，核心提供 `Scene::find_local_node(local_id)`、名称重载 `find_node(name)` 和 `Node::find_child(relative_path)`。局部身份复用索引，名称直接读取 ECS，子路径逐级查询 ChildOf；角色、脚本和相关编辑器代码复用这些接口。显式 Scene 查询允许恢复候选内部解析，WorldSystem 仍过滤 Published，脚本继续检查 Run/场景；隐藏或禁用不改变身份可查询性。完整语义见[Scene](runtime/class-diagrams/scene.md)与[Node](runtime/class-diagrams/node.md)。
- `scene_bake` 仅依赖核心资源加载契约，不再强制装配 `runtime/resource_graph`。调用方组件与作业各自持有实际加载的资源，独立测试通过不链接、不装配依赖图插件的捕获、IBL、光图烘焙和正式资源回载链路，验证结果见[测试文档](testing.md)。
- ModelingCommands 的路径校验与源文件 I/O 归私有 `modeling_source_io`，场景选择和命令事务仍由原模块负责。UiSystem 的输入与文档/焦点/捕获状态紧密相关，PhysicsSystem 的模拟和形状算法也仍在正确领域；后续有实际维护需求时按内部职责拆文件，保持既有系统与所有权边界。

## 模块入口

- [运行时](runtime/runtime.md)：框架、宿主、玩法和系统生命周期。
- [场景](runtime/scene.md)：Node、组件、层级、变换及保存与加载。
- [跨模块数据](runtime/data.md)：FlatBuffers 模型、GUID 引用与快照。
- [渲染](runtime/rendering.md)：bgfx、平台后端、材质、PBR 与 GPU 资源生命周期。
- [资源管线](editor/asset-pipeline.md)：meta、oneTBB、SQLite、增量转换和完整版本发布。
- [资源构建](plugins/editor/resource-build.md)：项目清单、依赖闭包、散文件和合并包。
- [物理](runtime/physics.md)、[音频](runtime/audio.md)：节点组件、固定步模拟、PCM 播放与清理边界。
- [导航](runtime/navigation.md)、[动画](runtime/animation.md)、[运行时 UI](runtime/ui.md)：各系统的依赖接入与能力范围。
- [脚本与插件](plugins/plugins.md)：虚拟机选择、共享绑定、Gameplay、热重载和 MCP。
- [编辑器](editor/editor.md)、[编辑器配置](editor/configuration.md)：环境、项目控制和工具设置。
- [项目与出包](editor/project.md)：项目目录契约、运行内容与平台部署。
