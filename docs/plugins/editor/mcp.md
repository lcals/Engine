# MCP 场景编辑

`McpSystem` 只处理协议、工具注册和参数检查。`ProjectMcpTools` 登记原有项目启停、资源构建、同步与状态工具；`EditorMcpTools` 登记桌面场景工具。各模块提供工具名称、参数模型和回调，宿主在 MCP 初始化前登记。新增模块不需要改协议分派函数；需要主线程操作的工具回调可复用 `EditorMcpTools::call_command`，由对应编辑器模块注册和执行命令。

| 工具 | 参数与作用 |
| --- | --- |
| `get_scene` | 可选 `offset`、`limit` 分页查询活动场景、节点和组件，待机返回 `scene: null` |
| `create_node` | `sceneId`、`name`、`component`（`empty` / `camera` / `light`）；创建带 Transform 的唯一命名节点 |
| `set_active_camera` | `sceneId`、`nodeId`；选择场景中的 Camera 节点 |
| `set_transform` | `sceneId`、`nodeId`，以及至少一项 `position` / `rotation` / `scale` 三分量数组 |
| `set_camera` | `sceneId`、`nodeId`；设置投影、视角、裁剪面、正交尺寸或 `frustumCullingEnabled` |
| `set_light` | `sceneId`、`nodeId`；设置颜色、强度、类型、范围或聚光半角 |
| `set_material_type` | `sceneId`、`nodeId`、`path`、可选 `slot`；按完整路径选择材质资源 |
| `set_material_parameter` | `sceneId`、`nodeId`、`name`、`type`、`value`、可选 `slot`；类型为 `float` / `v2` / `v3` / `v4` |
| `set_material_texture` | `sceneId`、`nodeId`、`name`、`path`、可选 `slot`；按采样器名设置贴图资源 |
| `set_collider` / `get_collider` | `sceneId`、`nodeId`；添加/编辑或查询 box、sphere、capsule 碰撞体及摩擦、弹性、触发器 |
| `set_rigid_body` / `get_rigid_body` | `sceneId`、`nodeId`；添加/编辑或查询运动类型、质量、重力、阻尼和速度 |
| `add_impulse` | `sceneId`、`nodeId`、`impulse` 三分量数组；提交动态刚体冲量，不可撤销 |
| `set_audio_source` / `get_audio_source` | `sceneId`、`nodeId`；添加/编辑或查询 clip 路径、音量、音调、循环和空间衰减 |
| `set_audio_listener` / `get_audio_listener` | `sceneId`、`nodeId`；添加/编辑或查询监听器音量 |
| `play_audio` / `pause_audio` / `stop_audio` / `get_audio_state` | `sceneId`、`nodeId`；播放控制或状态查询，不可撤销 |
| `instantiate_model` | `sceneId`、`path`、`name`；从模型资源创建节点层级，支持撤销重做 |
| `set_animator` / `get_animator` | `sceneId`、`nodeId`；配置独立动画资源、速度、循环及自动播放，或查询配置 |
| `play_animation` / `pause_animation` / `stop_animation` / `seek_animation` / `get_animation_state` | `sceneId`、`nodeId`；播放、保留姿态暂停、恢复静态网格停止、按 `seconds` 定位或查询状态 |
| `set_character_motor` / `get_character_motor` | `sceneId`、`nodeId`；配置或查询核心胶囊角色运动组件 |
| `set_character_controller` / `get_character_controller` | `sceneId`、`nodeId`；配置或查询玩家输入与可空相机引用，仅启用对应编辑器插件时注册 |
| `set_character_camera` / `get_character_camera` | `sceneId`、`nodeId`；在 Camera 节点配置独立跟随相机、`targetNodeId` 和避障参数 |
| `set_character_visual` / `get_character_visual` | `sceneId`、`nodeId`；标记身体、头部或第一人称手／武器子树 |
| `save_scene` | `sceneId`、`path`（相对于项目 `assets/` 的 `.scene` 路径）；保存完整场景及当前网格、材质源 |
| `execute_batch` | `commands` 数组，每项含 `command` 与 `arguments`；接受各模块已注册的可撤销修改，最多 64 项 |
| `undo` / `redo` | `sceneId`；撤销或重做一条编辑记录 |
| `get_editor_receipt` | `session`、`requestId`；查询原请求的持久回执 |
| `execute_project_script` | `session`、`runId`、`sourcePath`、`source`、可选 `arguments`；临时编译 TS，在当前运行实例调用默认导出并返回 JSON |
| `close_editor` | 可选 `delayMs`，整数 100～10000，默认 500；返回关闭已安排的回执，延迟后正常退出编辑器 |

场景与节点 ID 使用查询返回的十进制**字符串**，保留完整 64 位身份。创建后按唯一名称通过 `get_scene` 查询节点；创建或删除的重做可能改变运行时 ID，后续外部请求使用新查询值。`set_material_type` 支持 `builtin://materials/default-pbr`、`builtin://materials/default-unlit`、`builtin://materials/default-blinn-phong` 和项目材质路径；隐藏 Shader 字段不能通过具名参数工具修改。几何创建与拓扑编辑由[建模模块](modeling.md)提供。编辑结果不会自动写回源文件，显式 `save_scene` 才发布场景与资源。

```sh
python3 scripts/mcp_client.py --server build/ninja-engine-release/mcp_app --project projects/showcase --runtime-dir build/editor-runtime list
python3 scripts/mcp_client.py --server build/ninja-engine-release/mcp_app --project projects/showcase --runtime-dir build/editor-runtime call get_scene
# edit.json 使用查询到的 sceneId/nodeId，内容例如：
# {"sceneId":"1","nodeId":"42","position":[1,0,0]}
python3 scripts/mcp_client.py --server build/ninja-engine-release/mcp_app --project projects/showcase --runtime-dir build/editor-runtime call set_transform --arguments-file edit.json
```

`list` 使用标准 `tools/list`，输出当前构建实际注册的工具与完整参数 schema，不要求编辑器正在运行；可选插件未装配时相应工具不会出现。模块覆盖及已确认缺口见 [MCP 覆盖清单](mcp-coverage.md)。

通用 `call <工具名>` 接受 `--arguments '<JSON对象>'` 或 `--arguments-file <文件>`，二者互斥，省略时传空对象。示例 ID 只演示格式，实际操作必须使用当前查询值。

场景工具经 `editor-command.json` 交由编辑器主线程执行，MCP 不直接访问运行场景。可撤销修改生成编辑记录；保存与截图是独立文件操作，不进入撤销批次。批量修改失败按相反顺序回滚，成功批次只占一条记录；历史最多 128 条。停止、重启或切换场景后旧历史失效。玩法若已修改目标值，撤销会返回冲突，避免覆盖后来的业务状态；例如示例 Game 持续旋转方块时，旋转编辑可能产生此类冲突。

提交使用操作系统文件锁 `editor-command.lock`，进程退出自动释放；当前会话的待完成请求不会被覆盖。请求和回执均核对项目、会话及编号，单文件最多 1 MiB。等待 10 秒仍未确认时返回 `activation: pending` 及原请求身份，表示**结果未确认而非取消**。回执保存在 `editor-receipts/<session>/<requestId>.json`，最新快照为 `editor-receipt.json`；同一 ID 重放不重复执行。查询回执无需编辑器在线；查询成功不代表原修改成功，须检查其中的 `success` 和 `error`。`execute_batch` 的回执命令名为编辑器内部的 `batch`。

编辑器命令模块通过 `EditorCommands::register_command` 登记处理函数，修改返回一个实现 `EditorEdit::apply/revert` 的记录。宿主可借用 `EditorSystem::commands()` 在初始化后、进入主循环前登记模块；全部访问限于编辑器线程。运行时组件保持普通 C++ 接口，不引用 MCP、撤销系统或编辑器。对应类图见[命令执行](../../editor/class-diagrams/editor-commands.md)及[编辑器 MCP 工具](class-diagrams/editor-mcp-tools.md)。

## 渲染功能统一入口

`get_render_features` 返回当前世界运行请求/最近提交效果的只读汇总（`renderer.scope: "world_summary"`）。指定 `sceneId/nodeId` 时，还返回该 Camera 或 MeshRenderer 的真实组件属性（`scope: "component"`）；附带 `slot` 时查询指定材质。它区分 requested / supported / effective / reason，不把全场景合批结果当作选中对象的效果。

统一 `set_render_feature` 按所属组件路由；以下身份必须使用当前 `get_scene` 返回值：

```json
{"feature":"frustum_culling","enabled":true,"sceneId":"…","nodeId":"相机节点ID"}
{"feature":"static_instancing","enabled":true,"sceneId":"…","nodeId":"网格节点ID"}
{"feature":"static_lod","enabled":true,"sceneId":"…","nodeId":"网格节点ID"}
{"feature":"atmosphere","enabled":true,"sceneId":"…","nodeId":"环境节点ID"}
{"feature":"hex_tiling","enabled":true,"sceneId":"…","nodeId":"网格节点ID","slot":0}
```

前三项为可保存、可撤销的 Camera / MeshRenderer 属性，新增属性默认 false；允许在设备未初始化或当前设备不支持时保存 true，实际支持及生效另行查询。`get_mesh_render_settings` / `set_mesh_render_settings` 用 `sceneId/nodeId` 查询或编辑 `staticInstancingEnabled/staticLodEnabled`，每次只恢复实际编辑的字段，保留网格、材质及运行态变形。

天空要求指定当前活动环境，太阳通过原 `set_render_settings` 配置。对外 `atmosphereSunNodeId` 使用与 `get_scene` 相同的全局十进制 NodeID，`"0"` 清空；只读 `atmosphereSunLocalId` 仅用于诊断场景保存引用，不可传给 setter。Hex 只接受声明 `hexTilingEnabled` 参数的 Shader。材质查询的 `ready` 表示材质参数已准备，`effective:null` 明确未单独测量该物体的绘制，不能用其他物体的批次推断。

以下五项仅为临时 MCP 诊断状态，不传场景/节点/材质身份、不写场景或预制体，也不暴露给项目 TS：

```json
{"feature":"wireframe","enabled":true}
{"feature":"mesh_bounds","enabled":true}
{"feature":"camera_frusta","enabled":false}
{"feature":"debug_draw","enabled":true}
{"feature":"stats","enabled":true}
```

旧 `set_render_stats` 与统一 stats 共用唯一状态；当前设备不支持 wireframe 时开启会失败并保留旧值。

`get_render_submission_stats {}` 返回 `frameNumber`、逐 view 的候选/剔除/可见 Mesh、实际 draw、实例批次/数量与三角形。计数只覆盖本渲染模块的 scene mesh，不包括 UI、外部扩展或 bgfx 全设备统计；`gpuCompletion: "not_measured"` 明确没有测量 GPU 完成。CPU/GPU 耗时沿用现有 Tracy/bgfx 数据来源，不能把帧间墙钟时间当渲染耗时。

`draw_debug_lines` 使用实际主观察镜头，提交有限寿命的通用线段；每次最多 4096 条。返回 accepted 后仍需正常提交帧，截图继续通过 `capture_frame/get_frame_capture` 查询真正完成。

```json
{"lines":[{"start":[0,0,0],"end":[0,3,0],"color":1358926079,"depthTest":true}],"lifetimeFrames":120}
```

Frame/View uniform 作为 `implementation.frame_view_uniforms` 的只读实现能力查询，不是可切换画质效果。天空数值继续通过 `set_render_settings` 的 atmosphere 字段配置，Hex 尺度/混合通过正常具名材质参数配置。LOD 离线生成通过 `get_mesh_lod_import {"path":"meshes/source.mesh"}` 和 `set_mesh_lod_import` 修改源 meta、保留 GUID 并走正常导入；它与运行时 static_lod 开关分开保存。详见[统一功能归属与契约](render-feature-controls.md)和[静态模型 LOD](../../editor/static-model-lod.md)。

## 当前项目的 TS 执行

先调用 `get_status` 获取活动编辑器的 `session` 和 `runId`，再调用 `execute_project_script`。`sourcePath` 是相对 `assets/` 的虚拟 `.ts` 路径，用于类型检查和相对 import；它不会写成项目源文件或产生 meta。代码按项目 tsconfig 和 `engine.d.ts` 检查，在 MCP 工具进程编译，再在编辑器主线程交给当前 Run 的同一 VM 执行。

```ts
export default function(args: { label: string }, gameplay: unknown) {
    return { label: args.label, running: gameplay != null };
}
```

默认导出接收 `(arguments, gameplay)`，后者是当前活动玩法实例，不重新创建或初始化。可导入 `engine` 和当前 Run 已加载的相对项目模块（使用 `.js` 后缀），共享原有 Model 和模块状态。临时函数用完释放；同一路径的新请求不沿用上次代码。返回值须为同步 JSON 数据；异步业务先返回项目 ticket，再提交脚本查询状态。动态 import、顶层 await、未加载的项目模块和 Promise 返回不受支持。

源码、参数和返回结果分别最多 256 KiB。停止、重开或热重载后旧 `runId` 失效，编译期间发生换代也会在执行前拒绝。编译或运行错误只使本请求失败，不停止活动 Run；已发生的业务副作用不会自动撤销。代码仍须主动返回；工具等待超时不能中断 VM 无限循环。此入口只在桌面编辑器 MCP 装配，运行包不新增 Node、TypeScript 编译器或 MCP。

## UI 编辑与正常操作

独立 `editor/ui` 插件提供 11 个工具：文档绑定、控件树和状态查询、临时 View 修改、源 RML/meta 读写，以及有界输入序列。先用 `get_ui_documents` 和 `get_ui_tree` 获取当前 `documentId` / `elementId`；这些身份随文档销毁失效，同名控件重建后不能复用旧句柄。

- `set_ui_document` 修改预览中的 UiComponent，支持撤销和 `save_scene`。
- `set_ui_element` 只改当前 View，不修改 TS Model，也不保存源文档。
- `get_ui_source` 用 `path` 提交后台读取，用 `job` 查询；`save_ui_source` 带读取时的源/meta 哈希保存，保留 GUID，拒绝冲突。`source_saved` 与 `import: pending` 不代表已刷新画面。
- `ui_input` 提交真实指针、滚轮、焦点、键盘、Unicode 文字与跨帧等待；最多 256 个事件、10 秒，down/up 必须配对。拖动由 move/down/move/up 组合，`click_ui` 是同一路径的便利组合。
- `get_ui_input` 查询完成数和状态，`cancel_ui_input` 安全释放模拟按压。实体输入优先；失焦、文档失效及超时取消余下输入。

输入使用窗口逻辑坐标，经过 RmlUi 正常命中、焦点和事件分发，不直接调用 TS handler。遮挡、隐藏、禁用或相对鼠标模式会明确拒绝不适用的指针操作。输入作业完成后，继续查询 UI 或项目 TS 验证业务结果。详细参数以 `tools/list` 为准，行为契约见 [UI 编辑器类图](class-diagrams/ui-editor.md)。

## 延迟关闭编辑器

```sh
python3 scripts/mcp_client.py --server build/ninja-engine-release/mcp_app \
  --project projects/showcase --runtime-dir build/editor-runtime \
  call close_editor --arguments '{"delayMs":500}'
```

命令成功结果为 `{"scheduled":true,"delayMs":500}`（重复请求返回剩余延时）。这是关闭安排完成，不是进程已退出。回执成功写入现有命令通道后才开始计时；写入失败时保留请求重试。延时到期后通过正常 SDL 退出事件通知宿主清理：停止 UI 输入、烘焙和玩法，释放编辑器及运行系统。重复调用不推迟已安排的关闭；不可放进可撤销批次。命令不隐式保存未保存的场景编辑，需要保留时先执行 `save_scene`。

MCP 工具进程独立于编辑器，退出后仍可查询原回执；默认延时为工具返回回执留出窗口，并不表示远端客户端已确认收到。最终退出可核对项目状态中的 `projectState: "closed"` 和宿主退出码。

## 物理与音频编辑

运行时核心使用 `Collider` + `RigidBody` 表达物理对象，使用 `AudioSource` + `AudioListener` 表达音频对象，位置与朝向继续来自 `Transform`。Jolt 与 SDL 句柄只存在于对应系统实现；编辑器负责导入、保存和撤销，MCP 负责工具描述与转发。完整边界见[物理](../../runtime/physics.md)、[音频](../../runtime/audio.md)、[组件命令](../../editor/class-diagrams/component-commands.md)与[MCP 参数](class-diagrams/physics-audio-mcp-tools.md)。

`set_collider`、`set_rigid_body`、`set_audio_source`、`set_audio_listener` 可添加节点缺少的组件，支持批量与撤销；`get_*` 要求组件存在。动态模拟可能改变速度，撤销会检查完整组件快照并拒绝覆盖新状态。冲量与播放控制属于瞬态运行请求，不进入撤销批次。音频 `playback` 为 0 stopped、1 pending、2 playing、3 paused、4 failed；播放回执成功只表示请求接受，实际输出须查询后续状态。

音频源目前支持 WAV，转换为 `audio.fbs` 描述的交错 F32 PCM。相邻 `.meta` 使用 `[audio]` 的 `sample_rate`（默认 48000）、`channels`（默认 2）和 `gain`（默认 1），变化参与增量导入。生成物仍为无后缀 `cache/gen/<GUID>`，场景用 clip GUID 建立依赖，构建和同步共用现有资源清单。详见[音频导入](../../editor/class-diagrams/audio-importer.md)。

基础物理包括盒、球、胶囊，静态、运动学、动态刚体及冲量；动态刚体必须位于场景根节点。音频支持单/双声道、循环、音调、音量、暂停、距离衰减和左右声像。结束项目显式清理物理对象、流和逻辑设备；普通场景替换由各系统在下一次更新按场景身份清理。

[公寓物理与音频示例](../../../projects/showcase/docs/index.md#公寓物理与音频)复用已有场景与资源，增加静态家具碰撞体、动态落球、相机监听器及原创合成循环音频；示例配置不进入引擎核心。
