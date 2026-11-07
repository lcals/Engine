# 引擎类图

类图中的短类名按所在目录确定命名空间：核心为 `engine`，编辑器为 `engine::editor`，插件为 `engine::plugins::<插件名>`。核心不依赖编辑器或插件；编辑器库不依赖具体插件。插件通过底层接口提供扩展，由可执行宿主显式登记、装配；图中的借用接口不表示底层依赖其具体实现。

类图是架构评审入口，维护类的职责、所有权、公开方法和完整引用关系。构建与平台验证范围见[测试与范例](../testing.md)。

统一评审原则：**业务、运行宿主和编辑器都驱动最小核心，核心不承载调用方流程。** 评审每个方法时区分“提供能力”和“决定何时使用能力”，后者归调用方。完整边界见[调用方驱动核心](../architecture.md#调用方驱动核心)。

**单一版本原则：只维护当前版本，不编写或保留兼容代码。** 类图调整时同步列出被替换的接口、字段与调用方，确认后一次迁移并删除旧入口；不以兼容旧版本为由保留重复 API、别名或双轨逻辑。已有兼容保留说明列为待清理项。适用范围、数据更新和运行期身份保护见[单一版本维护](../architecture.md#单一版本维护)。

## 全仓精简评审

### 2026-11-07：模块边界与单一版本修复

本批类图、实现与调用方已同步，验证见[测试记录](../testing.md#2026-11-07模块边界与单一版本修复)：组件设置只保留 `get_settings(output)`；Node 删除返回默认值的 `info()`，保存使用 `build_info(output)`，局部身份直接使用 `local_id()`。Component 的存储快照与保存快照有不同语义，分别使用返回状态的 `get_data(output)` 与 `build_data(output)`，不合并数据含义。

动画播放快照包含资源与事件身份，脚本不读取 AnimatorRuntime；材质保存读取当前持有 Shader 的句柄；跨模块资源获取复用核心 ResourceReference 的显式所有权契约，纹理上传通过渲染模块的正式函数入口。编辑器准备阶段持有获取直至首次提交，撤销历史仍只保存版本元数据。删除 linearFog、废弃 Model 字段和旧类图入口，不保留兼容壳。

以下表格记录此前已完成的实现精简；本批公开签名以所属图的最新契约为准。

**状态：2026-11-07 按确认类图完成本轮代码精简。** 评审范围覆盖当前 144 份模块类图：运行时 64 份、编辑器 18 份、运行时插件 29 份、编辑器插件 33 份。下表列出的合并已落到代码，其余领域保持现有职责边界；已确认资源迁移的旧名称、签名和任务域说明一并校正。验证范围见[测试记录](../testing.md#2026-11-07全仓类图精简落地)。

| 范围 | 合并或精简目标 | 保留的职责与行为 |
| --- | --- | --- |
| [编辑器字段编辑](../editor/class-diagrams/component-commands.md#字段编辑合并目标) | TransformEdit、CameraEdit、LightEdit 合并为一个私有 PropertyEdit<DataT> 模板；与 ComponentEdit 共用 Target 身份解析 | 字段编辑只比较、修改选中字段；ComponentEdit 继续比较完整快照；创建重做的共享节点身份、批量回滚、Prefab 显式覆盖不变 |
| [建模计算](../plugins/editor/class-diagrams/editable-mesh.md) | EditableMesh、MeshPrimitives、MeshTopology、MeshGeometry 四个静态工具类收敛为 modeling::mesh 命名空间函数 | 原函数的参数、结果、校验、稳定 ID 和失败原子性不变；生成 Data、配置和选择结构不另造模型 |
| [MCP 工具登记](../plugins/editor/class-diagrams/editor-mcp-tools.md#共用工具登记目标) | 各模块重复的命令转发登记与对象 schema 组装共用两个普通函数 | 工具名称、参数、回执、超时和目标检查不变；有专门参数转换的工具仍自行处理 |
| [组件访问](../runtime/class-diagrams/component.md#组件实现精简目标) | 设置读取收敛为 get_settings；get_data 与 build_data 区分存储和保存快照；节点局部身份使用 local_id | 保持领域数据语义、组件实例校验和类型专属副作用，调用方统一检查读取失败 |
| [逐类脚本绑定](../plugins/runtime/class-diagrams/script-bindings.md#绑定公共步骤精简目标) | 创建组件、获取组件及参数数量检查复用私有函数；数值和字符串检查继续复用 BindingContext | 每类绑定与显式登记保留，Run 权限、原生错误、JS 外观和两种 VM 的共同语义不变 |
| [资源解码](../runtime/class-diagrams/resource-codecs.md#解码实现精简目标) | 核心 Codec 共用固定 FlatBuffers 解码步骤；业务绑定直接从一次获取读取 GUID | 每类格式、大小限制、错误和依赖校验不变；使用仍须先加载 |
| [资源记录](../plugins/runtime/class-diagrams/resource-graph.md#记录获取和在途合并) | 删除只写不读的 task_references；私有记录只在插件图定义一次 | 获取、依赖、请求三种计数保留；核心读取任务拥有工作状态，业务任务持有独立资源获取 |
| [引擎与宿主](../runtime/class-diagrams/engine-system.md)、[运行会话](../runtime/class-diagrams/gameplay-session.md)、[任务域](../runtime/class-diagrams/task-system.md) | 保留类边界；校正宿主签名和预算类型，初始化、失败清理继续复用已有生命周期入口 | Engine/System 管系统；RuntimeHost 管呈现及装配；GameplaySession 管玩法与部署；各模块任务域共享 worker |
| [世界与场景](../runtime/class-diagrams/ecs-scene.md)、[预制体](../editor/class-diagrams/prefab.md) | 保留 WorldSystem、Scene、Node、Component；不增加另一份运行数据或通用对象层 | Flecs 是唯一运行数据；节点和组件为身份句柄；结构编辑、字段编辑和完整快照各自保持冲突规则 |
| [渲染](../runtime/class-diagrams/render-system.md)、[影片](../runtime/class-diagrams/movie-player.md)、[音频](../runtime/class-diagrams/audio-system.md)、[字体](../runtime/class-diagrams/font-system.md)、[UI](../runtime/class-diagrams/ui-system.md) | 保留领域系统、GPU Pass 和播放/文档身份；本轮不合并不同执行阶段的类 | CPU 资源、GPU 资源、解码、设备声部、字体和文档有不同所有者及释放时机 |
| [输入](../runtime/class-diagrams/input-system.md)、[物理](../runtime/class-diagrams/physics-system.md)、[动画](../runtime/class-diagrams/animation-system.md)、[导航](../runtime/class-diagrams/navigation-system.md)及领域插件 | 保留核心能力与可替换插件；复用组件和绑定的公共步骤，不合并领域状态机 | 输入帧发布、物理步进、动画采样、导航提交与角色更新顺序不变；地形、破坏、特效的业务状态继续独立 |
| [编辑器项目环境](../editor/class-diagrams/editor-environment.md)、[资源管线](../editor/class-diagrams/asset-pipeline-system.md)及[插件](../plugins/plugins.md) | 保留 ProjectSystem/EditorEnvironment、导入器登记、构建和同步边界；格式处理复用已有编解码入口 | 项目校验、路径准备、导入、打包、部署及可选插件装配仍由各自所有者负责 |

上述已完成批次的迁移约定：同一函数或类的方法只在所属图定义，跨图仅引用；命名空间和函数节点明确标注，不能当作新增 C++ 类。建模函数一次替换旧静态类调用，不保留兼容壳；该批次其余公开契约除本节明确列出的函数增量外保持不变。保留尚未实现的影片 MCP、异步导入等规划标记，不把精简范围扩大成新增功能。

C++ 约束随迁移一并落实：C++20、无原生异常与 RTTI，失败返回明确状态且输出安全；独占对象用 unique_ptr，实际共享控制状态和身份单元才用 shared_ptr；借用和异步捕获有明确寿命。公共步骤使用普通函数或必要的简单模板，类型校验留在所属模块；不增加宏反射、通用 Manager 或另一层虚接口。const、noexcept、nodiscard、头文件自包含和命名格式按根 AGENTS.md 核对。

[统一输入模块](../runtime/input.md)由 TS 加载 / 覆盖 / 保存绑定并显式配对多手柄，原生采集设备、执行最终映射并处理虚拟触摸。接口入口为[输入系统](../runtime/class-diagrams/input-system.md)、[生成数据](../runtime/class-diagrams/input-data.md)、[输入脚本接口](../plugins/runtime/class-diagrams/input-script-api.md)，脚本声明位于 `include/engine.d.ts`；平台验证范围见[测试与范例](../testing.md)。

[角色与动画制作流程（规划）](../editor/character-production.md)记录 AI 离线加工、骨骼不匹配处理及引擎运行时动作复用的方向，并与[当前动画能力](../runtime/animation.md)区分。该规划尚未新增类或公开接口；后续骨架适配数据与重定向契约须先在所属类图及本索引中评审，不能把工具调研当成已实现契约。

## 资源系统

**统一资源设计及业务签名已由用户确认，代码按该契约一次迁移。** 核心提供文件与单项资源读取，资源插件组织依赖、共享、引用计数和卸载，业务显式加载、使用和释放。资源插件可由遵守同一契约的其他实现替代；核心和业务不绑定默认插件。脚本入口见 [Resources](../plugins/runtime/class-diagrams/resources-script-api.md)，实际验证范围见[测试记录](../testing.md)。

| 独立类图 | 评审内容 |
| --- | --- |
| [统一生命周期总图](../runtime/class-diagrams/resource-lifecycle.md) | 三层职责、可替换装配、就绪状态、计数、换代与释放 |
| [核心文件类](../runtime/class-diagrams/virtual-file-system.md) | 同步/异步范围读取、FileRead、取消及文件任务域 |
| [核心资源读取](../runtime/class-diagrams/resource-system.md) | 单项解析/校验、ResourceRead、独占候选，不维护业务计数 |
| [资源对象](../runtime/class-diagrams/resource.md) | Resource 基类与规范类型载荷，不集中增加类型字段 |
| [逐类型处理器](../runtime/class-diagrams/resource-codecs.md) | 21 种 AssetKind 的载荷、校验、登记与插件归属 |
| [高层加载契约](../runtime/class-diagrams/resource-loader.md) | ResourceLoader、显式 load/release、获取身份、异步结果与内存资源 |
| [资源生命周期插件](../plugins/runtime/class-diagrams/resource-graph.md) | 组合包装底层、依赖计划、在途合并、内部计数及反向卸载 |
| [业务与模块任务](../runtime/class-diagrams/resource-consumers.md) | 全类型加载/使用/释放、禁止隐式读取、模块独立任务域 |
| [资源目录](../runtime/class-diagrams/resource-catalog.md) | 既有元数据、GUID/路径索引、真实依赖与物理定位 |
| [编辑器资源构建](../plugins/editor/class-diagrams/asset-build-system.md) | 既有构建、增量输出和精简运行清单 |

核心不扫描、监听、导入、打包或区分调用方身份。编辑器提交完整目录，真机提交选定精简清单；业务统一请求高层接口。存储约定见[资源加载说明](../runtime/resource-loading.md)，跨层关系见[资源调用关系](references.md#资源调用关系)。

本组已确认契约统一采用显式加载、句柄获取及释放。各领域按[业务边界](../runtime/class-diagrams/resource-consumers.md)接入，脚本调用见[资源脚本接口](../plugins/runtime/class-diagrams/resources-script-api.md)。

## 当前宿主与导入模块

- [运行时线程与任务系统](../runtime/class-diagrams/task-system.md)：oneTBB 共享 worker、编辑器与运行时独立任务域，两级优先度、异步依赖批次与 Tracy。
- [帧率限制](../runtime/class-diagrams/frame-limiter.md)：核心整帧休眠能力，编辑器和项目选择上限。
- [运行宿主](../runtime/class-diagrams/runtime-host.md)：装配具体系统，处理窗口事件、暂停与帧提交。
- [运行设置](../runtime/class-diagrams/runtime-settings.md)：项目 TOML 仅由编辑器/打包读取，统一 JSON 交付运行宿主，核心接收类型配置。
- [脚本运行契约](../runtime/class-diagrams/script-runtime.md)：核心定义的执行接口与状态，宿主通过工厂选择插件。
- [玩法运行会话](../runtime/class-diagrams/gameplay-session.md)：协调脚本、场景、GPU 与项目资源的启动和释放。
- [核心 GUID 映射](../runtime/class-diagrams/guid-map.md)：编辑器与真机复用，完整 GUID 与逻辑路径双向查询。

- [逐类型资源导入](../editor/class-diagrams/asset-importers.md)：通用导入契约、显式登记与每类资源独立转换。
- [统一资源导入插件](../plugins/editor/class-diagrams/asset-import.md)：FBX、USD、字体、UI 与 JSON 归同一 editor/asset_import 插件，逐类型实现与宿主登记保持独立。
- [FBX 模型资源导入](../plugins/editor/class-diagrams/fbx-importer.md)：ufbx 解析、稳定子资源、骨骼与动画采样。
- [动画制作与源元数据](../plugins/editor/class-diagrams/animation-authoring.md)：socket、IK 与事件轨的源 meta 制作、同一 EditorCommands 事务与 MCP 转发。
- [USD 静态模型导入](../plugins/editor/class-diagrams/usd-import.md)：统一导入插件内的 USD 实现、自包含静态多边形及增量子网格。
- [字体资源导入](../plugins/editor/class-diagrams/font-importer.md)：统一导入插件内的 FontImporter 读取 TTF 和 meta，生成共享字形曲线与度量资源。
- [UI 资源导入](../plugins/editor/class-diagrams/ui-importer.md)：文档、字体和图片引用转换为标准运行资源依赖。
- [JSON 资源导入](../plugins/editor/class-diagrams/json-importer.md)：独立 `.json` 配置、严格 UTF-8 校验、原字节发布与空依赖。
- [建模资源导入插件](../plugins/editor/class-diagrams/modeling-mesh-importer.md)：宿主注入网格导入扩展，编辑器管线不依赖建模插件。
- [场景组件描述编解码](../editor/class-diagrams/scene-component-codecs.md)：组件 TOML 读取、验证和写出按类型拆分。
- [场景导入器](../editor/class-diagrams/scene-importer.md)：TOML 场景读写与生成数据校验。
- [音频导入器](../editor/class-diagrams/audio-importer.md)：按 meta 转换 WAV 为交错 PCM，生成 GUID 资源。
- [资源管线](../editor/class-diagrams/asset-pipeline-system.md)：meta 身份、SQLite 索引、依赖增量和 GUID 缓存发布。

## 影片播放

以下入口维护启动片头、业务全屏播放及导入的正式契约。现有实现采用 libvpx / VP9 与 FFmpeg 离线转码；2026-11-06 修复按图约束追帧、媒体时钟、纯状态查询、发布版本选择及启动资源闭包。影片 MCP、专用预览与异步导入批次仍是明确标注的后续规划。验证结果集中于 docs/testing.md。

| 独立类图 | 评审内容 |
| --- | --- |
| [播放器与帧呈现](../runtime/class-diagrams/movie-player.md) | MoviePlayer、播放身份与终态、YUV 帧借用、SIMD/AVX、RenderSystem 增量 |
| [影片资源](../runtime/class-diagrams/movie-data.md) | 唯一生成模型、MovieResourceCodec、视频载荷与独立 AudioClip 依赖 |
| [复用音频](../runtime/class-diagrams/movie-audio.md) | AudioSystem 无场景声部、共享设备、位置查询与清理边界 |
| [启动与业务装配](../runtime/class-diagrams/movie-startup.md) | 真正先窗口播放、后台准备、单帧呈现与主页切换条件 |
| [影片脚本绑定](../plugins/runtime/class-diagrams/movie-script-api.md) | 同一能力的脚本外观、Run 所属句柄与热重载清理 |
| [影片源资源导入](../plugins/editor/class-diagrams/movie-importer.md) | 原 MP4/MOV 保留、稳定子 GUID、meta、指纹和标准缓存 |
| [资源导入任务](../editor/class-diagrams/asset-import-jobs.md) | 原管线补开始/查询/取消，继续使用既有任务域与原子发布 |
| [影片 MCP 与预览](../plugins/editor/class-diagrams/movie-mcp-tools.md) | 命令通道、参数修改、预览和现有构建/同步工具复用 |

跨层拥有、借用与调用方向汇总见[影片关系](references.md#影片关系)。公开方法分别在所属类图定义，跨图只引用类和关系；规划页不得当作已实现能力。

## 场景与项目运行

- [ECS 场景与虚拟节点](../runtime/class-diagrams/ecs-scene.md)：Node/组件值句柄、Flecs 唯一运行数据、直接查询，以及预制体/脚本/撤销迁移边界；当前实现与分模块类图的统一入口。
- 场景中的全部对象必须通过 Node + Component 表达，包含相机与灯光；场景通过 GUID 组装独立 Mesh、材质与 Shader 资源。
- [场景](../runtime/class-diagrams/scene.md)：世界独占已发布场景及恢复候选，区分当前玩法场景与常驻容器；场景管理实体生命周期，真实数据归世界的 ECS。
- [预制体](../runtime/class-diagrams/prefab.md)：模板资源、稳定实例映射与显式属性覆盖；沿用 Node/Component，不改变 ECS 所有权。
- [预制体编辑与导入](../editor/class-diagrams/prefab.md)：子树/模型模板制作、字段合成、依赖发布及场景关联保存。
- [节点](../runtime/class-diagrams/node.md)：轻量身份与层级访问；按类型添加和查询组件值句柄，不拥有组件列表。
- [常驻节点与场景切换](../runtime/class-diagrams/persistent-nodes.md)：根节点及子树跨场景存活、原 UI/Controller 复用，以及完整停止时的清理边界。
- [组件创建登记](../runtime/class-diagrams/component-registry.md)：按生成类型登记工厂，节点泛型创建，组件自行恢复数据。
- [组件](../runtime/class-diagrams/component.md)：Component、Transform、Camera、Light、MeshRenderer 及当前参数。
- [环境与后处理组件](../runtime/class-diagrams/environment-components.md)：环境光、雾与清屏色归环境节点，曝光与色调映射归相机节点，阴影归灯光；预览与运行共用场景数据。
- [场景 GPU 烘焙插件](../plugins/editor/class-diagrams/scene-bake.md)：MCP 异步作业、GPU 环境预计算和场景同名目录资源发布；运行时只加载结果。
- [模型资源](../runtime/class-diagrams/model-resources.md)：模型节点层级、骨架/动画与子资源 GUID，核心读取统一生成数据。
- [特效脚本绑定](../plugins/runtime/class-diagrams/effects-script-api.md)：按可选插件登记的播放与资源控制。
- [特效系统与组件](../plugins/runtime/class-diagrams/effects.md)：独立 EffectSystem、EffectComponent、共享资源与实例播放生命周期。
- [特效导入与 MCP](../plugins/editor/class-diagrams/effect-importer.md)：官方效果字节、meta 依赖映射、可撤销编辑和播放命令。
- [动画组件](../runtime/class-diagrams/animator.md)：片段选择、双动作过渡、播放、暂停、定位及运行状态。
- [动画扩展实现评审](../runtime/scripted-animation-controller.md#已确认的接口评审记录)：当前已实现动画扩展的契约入口，Animator 完整方法统一见[动画组件](../runtime/class-diagrams/animator.md)；包含 [TS 状态机](../plugins/runtime/class-diagrams/animation-state-machine.md)、[重定向核心契约](../runtime/class-diagrams/animation-retargeting.md)、[可选重定向实现](../plugins/runtime/class-diagrams/animation-retarget.md)、[IK 与挂点](../runtime/class-diagrams/animation-rigging.md)、[事件注入](../runtime/class-diagrams/animation-events.md)。
- [角色物理组件](../runtime/class-diagrams/character-motor.md)：胶囊运动、地面/台阶与 Jolt 查询，不解释输入。
- [角色控制器](../plugins/runtime/class-diagrams/character-controller.md)：可选运行时输入与第一/第三人称相机策略。
- [物理组件](../runtime/class-diagrams/physics-components.md)：Collider、RigidBody、碰撞形状、运动设置与瞬态冲量。
- [分段静态网格碰撞](../runtime/class-diagrams/mesh-collision-data.md)：共享不可变块几何，复用 Jolt 子形状并原子替换。
- [音频组件](../runtime/class-diagrams/audio-components.md)：AudioSource、AudioListener、共享音频资源与播放状态。
- [字体系统与 3D 文字](../runtime/class-diagrams/font-system.md)：核心 FontSystem 共享曲线资源、文字排布和 GPU 绘制，普通节点挂载 TextComponent。
- [场景界面](../runtime/class-diagrams/ui-system.md)：普通节点挂载 UiComponent，RmlUi 文档内部控件不展开为场景节点。
- [材质](../runtime/class-diagrams/material.md)：内置/项目路径选择材质与 Shader，按名字和数值类型匹配参数。
- [场景数据模型](../runtime/class-diagrams/scene-data.md)：FlatBuffers 定义、C++/TS 生成及组件数据所有权。
- [编辑器环境](../editor/class-diagrams/editor-environment.md)：平台路径映射和编辑器目录提交。
- [渲染](../runtime/class-diagrams/render-system.md)：同一窗口的空白/PBR 场景呈现和每次运行 GPU 资源。
- [上游渲染能力接入](../runtime/upstream-rendering-features.md)：自动视锥剔除、Frame/View uniform、静态实例化、LOD、线框/调试、大气天空、Hex-Tiling 与统一 MCP；[控制接口](../runtime/class-diagrams/render-feature-controls.md)、[视图提交](../runtime/class-diagrams/render-submission.md)、[LOD](../editor/class-diagrams/static-model-lod.md)、[大气与材质](../runtime/class-diagrams/atmosphere-hex-material.md)。
- [分簇前向、反射与间接光](../runtime/class-diagrams/forward-lighting-reflections.md)：已评审的公开契约、GPU 所有权与材质绑定，包含局部阴影、SSR、平面反射、透明折射及 GI 消费。
- [场景间接光烘焙](../plugins/editor/class-diagrams/scene-lighting-bake.md)：已评审的 xatlas、lightmapper 适配、多轮反弹、探针与原子资源发布；[固定上游来源](../runtime/rendering-sources.md)。
- [渲染扩展](../runtime/class-diagrams/render-extension.md)：插件场景透明绘制位置、借用登记与 GPU 生命周期。
- [编辑器](../editor/class-diagrams/editor-system.md)：默认待机，主线程处理 MCP 启停，停止恢复空白。
- [场景预览](../editor/class-diagrams/scene-preview.md)：独立隐藏镜头、自由飞行、增量导入及未保存编辑保护；不推进玩法系统。
- [脚本公开接口](../runtime/class-diagrams/gameplay.md)：Gameplay 加载场景或动态创建节点与组件，运行停止或重载后旧引用安全失效。

- [绑定接口与注册表](../plugins/runtime/class-diagrams/script-bindings.md)：逐类实现 ScriptBinding，新增绑定只需增加类和注册项。

- [网格工具](../runtime/class-diagrams/mesh-utils.md)：验证几何及按上游算法生成切线。
- [贴图导入](../editor/class-diagrams/texture-importer.md)：按 meta 配置调用 BasisU 生成 GUID 资源。

## 脚本接口

- [运行时控制](../plugins/runtime/class-diagrams/runtime-controls-script-api.md)：在现有 WindowSystem、RenderSystem、WorldSystem 和资源加载链路补齐显示、退出、鼠标、暂停及单线程资源进度；底层复用已选成熟库，实现与 SDK 同步。

- [App 平台路径](../runtime/class-diagrams/app.md)：缓存、包内资源和持久数据具有跨平台一致含义。
- [字符串 KV](../runtime/class-diagrams/key-value-store.md)：原生通用存取、每 Run 缓存、显式保存和脚本外观；游戏数据内容与版本由项目维护。
- [KV 脚本接口](../plugins/runtime/class-diagrams/storage-script-api.md)：Storage 静态门面、逐类绑定与回调边界。
- [JSON 资源脚本接口](../plugins/runtime/class-diagrams/json-script-api.md)：`Json.Load` 在活动回调读取标准资源，返回普通 JavaScript 值。
- [日志系统](../runtime/class-diagrams/log-system.md)：宿主管理的单例，默认写入磁盘。
- [项目玩法入口](../runtime/class-diagrams/gameplay.md)：项目 `Game` 继承 `Gameplay`，可承载项目业务与场景流转。
- [世界暂停方案](../runtime/world-pause.md)：冻结自动模拟并继续 UI、输入和菜单，保留既有系统装配与调度框架。
- [资源同步](../plugins/editor/class-diagrams/asset-sync-system.md)：完整批次发布到持久资源版本目录。

- [渲染脚本接口](../plugins/runtime/class-diagrams/render-script-api.md)：Camera、Light、Environment、PostProcess、MeshRenderer 与 Material 的组件/材质属性；设备显示设置另见运行时控制。
- [动画脚本接口](../plugins/runtime/class-diagrams/animation-script-api.md)：逐类 Animator 绑定、CrossFade 及播放状态；SetAnimation 与切换预检的区别。
- [物理与音频脚本接口](../plugins/runtime/class-diagrams/physics-audio-script-api.md)：组件创建、设置、冲量与播放控制，复用原生能力。
- [导航脚本接口](../plugins/runtime/class-diagrams/navigation-script-api.md)：显式提交静态网格、局部重建、完整路径查询及场景生命周期清理。
- [多角色导航](../plugins/runtime/class-diagrams/navigation-crowd.md)：DetourCrowd 代理与场景组件同步，原生路径跟随、避让和位置更新。
- [导航代理编解码](../plugins/editor/class-diagrams/navigation-crowd.md)：可选编辑器插件读写代理参数；瞬态目标与状态不持久化。

- [地形脚本接口](../plugins/runtime/class-diagrams/terrain-script-api.md)：条件装配地形组件绑定、球形挖填、固定槽位保存与恢复。

## 建筑破坏插件

- [组件与系统](../plugins/runtime/class-diagrams/destruction.md)：普通节点绑定破坏资源，世界子系统拥有瞬态连通组，调用核心物理能力。
- [Blast 损伤图](../plugins/runtime/class-diagrams/destruction-graph.md)：官方图、部分损伤、支撑拆分与候选状态克隆，不依赖渲染或编辑器。
- [动态碎片退休](../plugins/runtime/class-diagrams/fragment-retirement.md)：按源组件显式执行动态碎片数量/寿命预算，复用 Blast actor 释放与 Jolt 回收，保持快照和后续破坏一致。
- [官方预破碎](../plugins/editor/class-diagrams/destruction-authoring.md)：参数盒体切片/Voronoi 与连接生成，离线依赖留在编辑器插件。
- [导入、编辑事务与 MCP](../plugins/editor/class-diagrams/destruction-editor.md)：独立资源、场景 GUID、完整状态撤销及工具转发。

步骤 2 代码已接入，Mac 定向测试、MCP 预览和菜单/UI 玩法已通过；完整结果及剩余范围见[步骤 2 验证记录](../plugins/runtime/terrain-destruction-feasibility.md#步骤-2-当前实现与验证范围)。步骤 3 已联通地基查询、物理撞击快照、二次损伤及特效/音频播放，见[步骤 3 验证记录](../plugins/runtime/terrain-destruction-feasibility.md#步骤-3-综合联动验证)。类图表达当前契约，不代表完整建筑力学或全部平台已经验收。

## MCP 与编辑器命令

- [功能覆盖与缺口](../plugins/editor/mcp-coverage.md)：实际注册工具、尚缺参数、范例和验证边界。
- [项目 TS 执行](../plugins/runtime/class-diagrams/project-script-execution.md)：MCP 直接提交 TS、编辑器编译、活动 Run 内执行及 JSON 结果，不要求项目逐项注册操作。
- [UI 编辑器插件](../plugins/editor/class-diagrams/ui-editor.md)：源资源编辑、实际控件查询，以及指针、键盘、文字和拖动等正常输入模拟。
- [渲染 MCP 补齐](../plugins/editor/class-diagrams/render-mcp-tools.md)：SSR、局部阴影、平面反射、透明混合与间接光资源绑定。

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
| 3 | [性能观测接入](../runtime/class-diagrams/performance-system.md) | 宿主初始化 Tracy 线程名称；各模块提交 CPU 区域、帧标记和 bgfx 统计，第三方不进入公开接口 |
| 4          | [世界系统](../runtime/class-diagrams/world-system.md)    | Engine 独占；flecs 世界、场景和子系统生命周期            |
| 5          | [渲染系统](../runtime/class-diagrams/render-system.md)   | Engine 独占；bgfx 设备、空白帧和资源网格 PBR             |

## 世界子系统

| 顺序 | 独立类图                                                   | 当前依赖       |
| ---- | ---------------------------------------------------------- | -------------- |
| 1    | [字体系统](../runtime/class-diagrams/font-system.md) | 核心字体资源与 bgfx 曲线绘制 |
| 2    | [界面系统](../runtime/class-diagrams/ui-system.md)         | RmlUi；借用核心 FontSystem |
| 3    | [导航系统](../runtime/class-diagrams/navigation-system.md) | Recast、Detour、DetourCrowd |
| 4    | [脚本执行契约](../runtime/class-diagrams/script-runtime.md) | 可选宿主工厂；随后登记玩家输入插件 |
| 5    | [物理系统](../runtime/class-diagrams/physics-system.md)    | Jolt；消费本帧意图 |
| 6    | [动画系统](../runtime/class-diagrams/animation-system.md)  | ozz-animation；随后登记独立相机插件 |
| 7    | [音频系统](../runtime/class-diagrams/audio-system.md) | SDL3 Audio；使用最终空间位置 |

[脚本系统](../plugins/runtime/class-diagrams/script-system.md)由宿主通过核心工厂装配，QuickJS/V8 编译期二选一。[虚拟机中间层](../plugins/runtime/class-diagrams/script-vm.md)维护单后端构建和每 Run 环境；[逐类绑定](../plugins/runtime/class-diagrams/script-bindings.md)在两种后端间共用，不向核心增加虚拟机类型。

## 宿主模块

- [Android 宿主](../runtime/class-diagrams/android-host.md)：SDL3 Activity 加载原生引擎，Activity 与 Surface 在销毁前同步暂停渲染；不要求编辑器项目。

- [项目系统](../editor/class-diagrams/project-system.md)：编辑器宿主先检查项目根目录及 settings、cache、assets；接入 toml++。
- [编辑器系统](../editor/class-diagrams/editor-system.md)：借用已就绪的编辑器环境，创建 Dear ImGui 上下文，支持编辑器子系统。
- `engine_app` 使用默认配置即可启动；`editor_app` 必须传入 `--project <path>`，先验证项目再启动窗口。编辑器默认空白待机，MCP 启动后项目占满同一窗口，停止后恢复空白；尚未绘制编辑器业务界面。
- [资源管线系统](../editor/class-diagrams/asset-pipeline-system.md)：oneTBB、SQLite、efsw、meshoptimizer；由资源工具宿主单独管理。
- shaderc、texturec、geometryc 是构建提供的宿主工具；fmt 用于基础格式化。它们不引入额外引擎包装类。
- 运行时不反向依赖编辑器、离线资源管线或具体游戏。Android 打包参考 SDL3 自带 Android 工程；APK 内置资源通过 SDL 文件流读取，平台验证范围见[测试与范例](../testing.md)。

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
