# 插件目录与构建登记

插件按运行时与编辑器划分，独立选择、独立编译。引擎基础接口留在 `runtime/`，项目环境、通用编辑命令、Task 和资源管线留在 `editor/`；插件通过这些接口提供具体策略或扩展能力。

| 范围 | 源码与头文件下的相对目录 | 提供的能力 |
| --- | --- | --- |
| 运行时资源依赖图 | `plugins/runtime/resource_graph/` | 必要依赖加载、共享缓存及按引用反向释放，实现核心加载接口 |
| 运行时脚本 | `plugins/runtime/script/` | QuickJS/V8 二选一、JS 模块与玩法执行、共享逐类绑定，实现核心 ScriptRuntime |
| 运行时角色控制器 | `plugins/runtime/character_controller/` | 输入与第一/第三人称相机策略，调用核心 CharacterMotor |
| 运行时多角色导航 | `plugins/runtime/navigation_crowd/` | NavigationAgent、原生目标消费与 Transform 同步，调用核心 DetourCrowd |
| 编辑器多角色导航 | `plugins/editor/navigation_crowd/` | 导航代理配置的 TOML 编解码 |
| 运行时特效 | `plugins/runtime/effects/` | 独立 EffectSystem、节点 EffectComponent、官方 Effekseer 模拟与 efkbgfx 绘制 |
| 编辑器特效 | `plugins/editor/effects/` | 特效资源导入、meta 依赖映射、组件编解码、可撤销编辑与 MCP |
| 运行时地形 | `plugins/runtime/terrain/` | Transvoxel 普通单元、同步体积修改与分块网格缓存、瞬态显示/碰撞提交和状态保存 |
| 编辑器地形 | `plugins/editor/terrain/` | 参数化 `.terrain` 导入、组件编解码、可撤销设置/笔刷/恢复及 MCP |
| 运行时建筑破坏 | `plugins/runtime/destruction/` | 官方 Blast 支撑图/损伤/拆分、瞬态组节点、Jolt 凸形复合碰撞与完整状态快照 |
| 编辑器建筑破坏 | `plugins/editor/destruction/` | 官方 Authoring 盒体切片/Voronoi、`.destruction` 导入、组件 codec、可撤销损伤/设置及 MCP |
| 编辑器资源导入 | `plugins/editor/asset_import/` | FBX、USD、字体、UI 与 JSON 独立格式实现，统一目标与宿主登记 |
| 编辑器 UI 操作 | `plugins/editor/ui/` | 文档绑定、控件查询、源码读写及正常输入模拟；可选 MCP 适配 |
| 编辑器角色控制器 | `plugins/editor/character_controller/` | 组件 TOML、撤销编辑与 MCP 适配，依赖对应运行时插件 |
| 编辑器建模 | `plugins/editor/modeling/` | 纯几何、拓扑、UV、网格编译、模型与节点映射、选择、撤销、源资源保存及本模块 MCP 适配 |
| 编辑器 MCP | `plugins/editor/mcp/` | MCP 协议、工具注册、参数检查、项目/场景/渲染工具和固定目标通道 |
| 编辑器资源构建 | `plugins/editor/asset_build/` | 项目打包清单、增量散文件/合并包及精简运行清单 |
| 编辑器资源同步 | `plugins/editor/asset_sync/` | 向本机或 Android 调试包发布完整资源版本 |
| 编辑器场景烘焙 | `plugins/editor/scene_bake/` | GPU IBL、Lightmap 与 SH9 探针烘焙，标准资源发布与 MCP 作业控制 |

头文件以完整作用域引用，例如 `plugins/editor/modeling/editable_mesh.hpp`、`plugins/editor/mcp/editor_mcp_tools.hpp`。纯运行时插件不得引用编辑器基础或编辑器插件；编辑器插件可以组合运行时插件和编辑器基础。核心通过自己的抽象接口使用可选实现，不 include 或链接具体插件。

## 插件清单

地形与建筑破坏按各自的运行时/编辑器插件装配，现行职责与跨插件协作见[联动契约](runtime/terrain-destruction.md)，制作流程见[地形与破坏制作](editor/terrain-destruction-authoring.md)。历史独立 ARM 探针与正式插件的测试范围分别记录于[技术研究](runtime/terrain-destruction-feasibility.md)；平台结果以[测试文档](../testing.md)为准。

`cmake/plugins/runtime/<name>.cmake` 与 `cmake/plugins/editor/<name>.cmake` 分别登记插件。**一插件一份 CMake**，同一文件声明本插件目标、源码、依赖和创建入口；中央注册器只读取登记、解析选择和依赖，不替插件维护源码列表。

- `ENGINE_RUNTIME_PLUGINS`：请求启用的运行时插件列表，运行时依赖开启时默认 `resource_graph`、`character_controller`、`navigation_crowd`、`effects`、`terrain` 与 `destruction`，脚本运行开启时再加入 `script`；显式设置优先。
- `ENGINE_EDITOR_PLUGINS`：请求启用的编辑器插件列表，默认根据已启用依赖能力选择 `modeling`、`asset_build`、`asset_sync`、`mcp`，资源管线开启时还加入 `asset_import`、`character_controller`、`navigation_crowd`、`effects`、`terrain` 与 `destruction`；完整桌面编辑器还加入 `scene_bake`、`ui`，可显式设置为空。
- 登记器解析依赖闭包后创建目标；依赖先于使用者建立，未选且不在闭包内的插件不创建目标。
- 明确移除一个插件时，同时核对有无已选插件依赖它；不能一边依赖它、一边声称它已禁用。
- 宿主只装配已创建的插件目标，核心基础构建不能被未选业务插件的源文件、依赖或头文件阻塞。

清单在 `project()` 前解析，已选插件所需的 vcpkg feature 在工具链安装依赖前加入。当前默认和依赖如下；显式缓存值优先，修改已有构建配置时应同时传入需要的清单值。

| 插件 | 默认加入条件 | 声明的插件依赖 |
| --- | --- | --- |
| `runtime/resource_graph` | `ENGINE_ENABLE_RUNTIME_DEPS=ON` | 无 |
| `runtime/character_controller` | `ENGINE_ENABLE_RUNTIME_DEPS=ON` | 无；调用核心世界、物理和窗口 |
| `runtime/navigation_crowd` | `ENGINE_ENABLE_RUNTIME_DEPS=ON` | 无；调用核心世界与导航，不依赖玩家控制器或 Jolt |
| `editor/navigation_crowd` | 非设备构建，资源管线依赖开启 | `runtime/navigation_crowd` |
| `runtime/effects` | `ENGINE_ENABLE_RUNTIME_DEPS=ON` | 无；固定版本 Effekseer 和 efkbgfx，复用核心渲染扩展 |
| `editor/effects` | 非设备构建，资源管线依赖开启 | `runtime/effects` |
| `runtime/terrain` | `ENGINE_ENABLE_RUNTIME_DEPS=ON` | 无；固定版本 Transvoxel，调用核心世界、物理和资源接口 |
| `editor/terrain` | 非设备构建，资源管线依赖开启 | `runtime/terrain` |
| `runtime/destruction` | `ENGINE_ENABLE_RUNTIME_DEPS=ON` | 无；固定 Blast CPU low-level/ExtShaders，使用核心 Jolt 凸形复合碰撞 |
| `editor/destruction` | 非设备构建，资源管线依赖开启 | `runtime/destruction`；官方 ExtAuthoring 和 `destruction-authoring` vcpkg feature（Boost Multiprecision） |
| `editor/asset_import` | 非设备构建，资源管线依赖开启 | 无；统一声明 ufbx、OpenUSD、FreeType 与格式校验依赖，vcpkg features 为 `font-import`、`usd-import` |
| `editor/ui` | 桌面编辑器，运行时及资源管线依赖开启 | `editor/asset_import`；选中 MCP 时额外生成 UI 工具适配目标 |
| `editor/scene_bake` | 桌面编辑器，运行时及资源管线依赖开启 | `runtime/resource_graph`；运行包不包含此插件 |
| `editor/character_controller` | 非设备构建，资源管线依赖开启 | `runtime/character_controller` |
| `runtime/script` | `ENGINE_ENABLE_SCRIPT_RUNTIME=ON` | 无；需要运行时依赖和所选 JS 后端，默认 QuickJS |
| `editor/modeling` | 非设备构建，编辑器或资源管线依赖开启 | 无；需要运行时及建模第三方依赖 |
| `editor/asset_build` | 非设备构建，资源管线依赖开启 | 无；需要资源管线基础 |
| `editor/asset_sync` | 非设备构建，脚本运行开启，且编辑器或资源管线依赖开启 | 无；显式选择只要求运行时依赖 |
| `editor/mcp` | 非设备构建，脚本运行及资源管线依赖均开启 | `editor/asset_build`、`editor/asset_sync` |

Android/iOS 构建拒绝编辑器插件。未知插件、依赖环、缺少前置配置或运行时插件反向依赖编辑器均在配置时失败。`modeling` 插件提供几何目标；编辑器基础存在时再提供建模命令目标，导入管线存在时接入源网格编译。MCP 与建模同时启用时建立建模工具适配目标；几何计算不因此依赖 MCP。

这是构建期插件装配；不据此声明已提供 DLL 热插拔、C++ 模块热重载或状态迁移。插件 CMake 开关、虚拟机编译期选择与玩法热重载是不同机制。配置命令见[插件选择](#插件选择)，实际验证范围见[测试与范例](../testing.md)。

## 核心与调用方

命名空间分别为核心 `engine`、编辑器 `engine::editor`、插件 `engine::plugins::<插件名>`。插件可向下调用核心与适用的编辑器接口；编辑器基础库不引用插件。具体装配只在宿主进行，不能由插件 CMake 给核心或编辑器追加反向链接。

建模的权威源网格由 `ModelingMeshImporter` 处理，宿主通过 `AssetPipelineSystem::set_importer` 将它交给编辑器注册表。基础 `MeshImporter` 保留普通网格导入；未启用插件时，遇到建模权威源明确失败。导入器身份参与增量指纹，移除插件后不能复用此前插件生成的缓存绕过检查。详见[建模资源导入](editor/class-diagrams/modeling-mesh-importer.md)。

资源核心保留目录、GUID/VFS、单项读取与解析。`ResourceLoader` 定义加载与查询接口；运行时 `resource_graph` 插件通过相同接口组合单项能力、加载依赖并共享资源。业务只请求顶级对象，依赖策略不进入 Engine、资源文件底层或 MCP。

字体运行时属于核心 `runtime/font/`，字体源导入和 UI 文档导入分别由统一 `asset_import` 插件内的 FontImporter 与 UiImporter 实现。UI 和 3D 文字共用同一字体资源，运行包只消费转换后的轮廓与度量。UiImporter 生成独立 UI 文档资源，由普通 Node 上的 UiComponent 使用；控件树属于文档内部，不改变场景节点层级。宿主逐类型登记导入器，编辑器基础和核心不反向链接插件，见[统一资源导入](editor/asset-import.md)。

MCP 插件中的协议目标只拥有描述、输入模型与回调。编辑器业务模块自行注册工具，场景命令通过文件通道进入编辑器主线程；组件继续提供普通 C++ 方法。超时返回原请求身份，客户端查询完成回执，不重复执行未确认操作。

脚本玩法、VM 适配与共享绑定位于 `plugins/runtime/script/`。核心 [ScriptRuntime](../runtime/class-diagrams/script-runtime.md) 只声明执行与状态契约；宿主通过工厂创建所选插件，世界统一管理生命周期。RuntimeHost 和 GameplaySession 不链接具体脚本插件。原生绑定实现统一接口并显式登记，公开声明维护于 [include/engine.d.ts](../../include/engine.d.ts)。Gameplay 是场景外项目入口；场景对象使用 Node + Component。热重载由玩法会话驱动，MCP 同步资源并控制开关，不直接重载虚拟机。

`ENGINE_SCRIPT_BACKEND=quickjs|v8` 只在配置时选择，默认为 `quickjs`；插件目标只定义选中的 `ENGINE_SCRIPT_BACKEND_QUICKJS` 或 `ENGINE_SCRIPT_BACKEND_V8` 宏。注册表通过 [ScriptVm](runtime/class-diagrams/script-vm.md) 登记同一套绑定，没有运行时选择后端的 API。QuickJS 构建只启用 `quickjs` vcpkg feature，不准备或链接 V8/ICU；V8 构建不查找或链接 QuickJS。两种后端分别在独立构建目录保留缓存，宿主脚本准备入口统一为 `engine_script_prepare`。

V8 的 WebAssembly、Intl/ICU、Temporal、系统 instrumentation 在全部平台默认关闭，JIT 保留，默认包无 `icudtl.dat`。每个 Run 独占 ScriptVm；QuickJS 拥有自己的 runtime/context，V8 拥有自己的 isolate/context，只有 V8 platform 按进程共享。重载释放旧 Run 的任务与绑定，不迁移 VM 句柄。

## 运行包与调试工具

建模纯算法虽然不依赖编辑器状态，仍属于编辑器建模插件：当前用途是生产模型源资源和运行 Mesh，正式运行包只读取编译结果。MCP 整体属于编辑器工具，通用协议与业务适配在插件内分目标保持依赖边界，不需要为了目录分层把协议装入游戏。

当前 Android 同步由电脑上的 MCP 经 adb 完成；设备不运行 MCP 服务。未来如需设备端调试插件，必须明确区分正式运行与调试运行的构建用途，正式包排除调试目标、接口和依赖。本轮不预建不存在的设备调试插件。

## 评审入口

- [脚本运行契约](../runtime/class-diagrams/script-runtime.md)与[脚本插件](runtime/class-diagrams/script-system.md)。
- [资源加载接口](../runtime/class-diagrams/resource-loader.md)与[资源依赖图插件](runtime/class-diagrams/resource-graph.md)。
- [建模功能与边界](editor/modeling.md)、[建模编辑业务](editor/class-diagrams/modeling-commands.md)。
- [MCP 协议](editor/class-diagrams/mcp-system.md)、[编辑器 MCP](editor/class-diagrams/editor-mcp-tools.md)。
- [场景 GPU 烘焙](editor/class-diagrams/scene-bake.md)。
- [地形运行时](runtime/class-diagrams/terrain.md)、[地形制作](editor/class-diagrams/terrain-editor.md)与[地形脚本接口](runtime/class-diagrams/terrain-script-api.md)。
- [建筑破坏组件与系统](runtime/class-diagrams/destruction.md)、[Blast 图](runtime/class-diagrams/destruction-graph.md)、[官方预破碎](editor/class-diagrams/destruction-authoring.md)与[编辑器事务/MCP](editor/class-diagrams/destruction-editor.md)。
- [资源构建](editor/resource-build.md)、[资源同步](editor/class-diagrams/asset-sync-system.md)。
- [统一资源导入](editor/asset-import.md)与[插件归属类图](editor/class-diagrams/asset-import.md)：FBX/USD/字体/UI/JSON 五种实现共用一个编辑器插件，保留类型职责及现有接口。
- [字体导入](editor/class-diagrams/font-importer.md)、[UI 导入](editor/class-diagrams/ui-importer.md)、[核心字体](../runtime/class-diagrams/font-system.md)与[场景界面](../runtime/class-diagrams/ui-system.md)。
- [JSON 导入](editor/class-diagrams/json-importer.md)与[JSON 脚本接口](runtime/class-diagrams/json-script-api.md)：统一资源导入插件内校验配置，原生资源载荷和静态脚本读取复用现有契约。
- [跨层完整引用关系](../class-diagrams/references.md)。

模型格式以导入器 id 区分，FBX/USD 可同时产出 `model`。宿主通过 `add_importer` 装配新格式，现有同 id 才使用 `set_importer`；管线统一管理子 GUID、缓存和发布。格式解析器不进入运行包，后续格式遵循同一扩展契约，见[FBX](editor/class-diagrams/fbx-importer.md)及[USD](editor/class-diagrams/usd-import.md)。

## 特效系统

特效由独立 `EffectSystem` 运行时插件统一推进和绘制；普通 Node 挂载 `EffectComponent`，引用独立特效资源。组件保存播放参数与实例身份，系统拥有第三方 Manager、每实例句柄以及共享效果资源。引擎 `RenderSystem` 只登记 `RenderExtension`，向插件提供当前场景、镜头与绘制阶段，不识别 Effekseer 类型。编辑器基础只负责通用导入调度和命令事务；特效导入、资源映射及 MCP 工具归编辑器特效插件。参见[运行时类图](runtime/class-diagrams/effects.md)与[导入类图](editor/class-diagrams/effect-importer.md)。

## 建筑破坏系统

`DestructionSystem` 作为世界子系统管理独立的 `DestructionComponent`，同步提交官方 Blast 拆分后的瞬态渲染/碰撞组；核心物理只提供通用凸包与 compound 形状。编辑器插件拥有离线 Authoring、导入器、组件 codec 和撤销命令。MCP 工具目标只在选择 MCP 时建立，不进入设备运行包；Boost、Authoring 和 PhysX 均不进入正式运行时。

当前制作输入为最多 256 块的参数盒体；根节点须使用正等比缩放。showcase 第七入口验证墙、柱和塔的连接损伤，地形地基联动、重力应力、撞击再破坏和有界异步损伤已接入，详见[破坏模块](runtime/destruction.md)。Mac 定向测试、MCP 预览和菜单/UI 玩法已通过；具体事实与其他平台范围见[步骤 2 验证记录](runtime/terrain-destruction-feasibility.md#步骤-2-当前实现与验证范围)，不能由插件清单推断平台验收完成。

## 插件选择

插件由 CMake 清单选择，不使用额外 JSON 清单。每个插件在 `cmake/plugins/<runtime|editor>/<name>.cmake` 声明自己的目标、源码和依赖；注册器解析依赖闭包后才创建目标，宿主按实际目标装配。运行时插件包括 `resource_graph`、`script` 与可选角色输入/相机控制器；建模（包括纯几何）、MCP（包括协议）、资源构建、同步和统一资源导入均属于编辑器插件。

例如，使用已准备好的依赖配置纯运行时窗口且不启用资源依赖图：

```sh
cmake --preset ninja-release -DENGINE_RUNTIME_PLUGINS= -DENGINE_EDITOR_PLUGINS=
cmake --build --preset ninja-release
ctest --preset ninja-release
```

如需恢复资源依赖图，在配置时传入 `-DENGINE_RUNTIME_PLUGINS=resource_graph`；完整脚本运行使用 `'-DENGINE_RUNTIME_PLUGINS=resource_graph;script'`，默认选择 QuickJS，显式传入 `-DENGINE_SCRIPT_BACKEND=v8` 才选择 V8。依赖闭包会加入已选插件所需的其他插件；要排除某插件，必须同时移除依赖它的入口。完整能力清单见[插件说明](plugins.md)。核心 `ResourceSystem` 始终可单项加载，完整依赖加载与共享释放由选中的 `ResourceGraphSystem` 提供。

已有构建目录会保留此前的插件选择。更新代码后若要采用当前默认清单，可在同一 Preset 的配置命令追加 `-U ENGINE_RUNTIME_PLUGINS -U ENGINE_EDITOR_PLUGINS`；自定义清单继续显式指定。

脚本插件位于 `plugins/runtime/script/`，包含 `ScriptSystem`、编译期选定的虚拟机执行与共享逐类绑定。核心只保留 `ScriptRuntime` / `ScriptStatus` 契约，`RuntimeHost` 通过宿主提供的可空工厂装配；关闭插件不链接脚本虚拟机，仍可运行窗口、渲染、编辑器及资源导入，依赖脚本的项目启动会明确返回失败。`save_scene` 需要资源管线；保存已有资源引用不要求建模插件，只有动态网格导出需要建模写入回调。

MCP 在电脑端经 adb 同步 Android 调试包，设备端不运行 MCP；正式游戏不链接建模、MCP 或编辑器构建/同步工具。仅开启运行时和脚本不会默认启用资源同步插件；默认同步插件还要求编辑器或资源管线能力。未来设备调试插件必须按构建用途区分正式运行与调试运行，不能仅靠程序中的运行开关隐藏调试接口。这些开关是构建期选择，不表示支持 C++ 插件热插拔。

CMake 的其余职责分别位于 `install/`、`dependencies/`、`core/`、`editor/`、`platforms/`。新增插件在自己的声明文件维护源码，不向核心中央清单追加业务文件。

## 使用指南

- [脚本](runtime/scripting.md)、[角色控制](runtime/character-controller.md)、[群体导航](runtime/navigation-crowd.md)。
- [特效](runtime/effects.md)、[体积地形](runtime/terrain.md)、[建筑破坏](runtime/destruction.md)。
- [MCP 场景编辑](editor/mcp.md)、[模型导入](editor/model-import.md)、[光照烘焙](editor/scene-bake.md)。
- [完整资源同步](editor/resource-sync.md)、[公开范例与验证](../testing.md)。
