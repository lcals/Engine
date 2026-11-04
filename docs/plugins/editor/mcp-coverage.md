# MCP 功能覆盖

[MCP 用法](mcp.md) · [公开范例测试](../../testing.md) · [类图索引](../../class-diagrams/index.md)

## 核对方式

通过当前构建的标准 `tools/list` 核对实际装配的工具、描述与参数，不在文档维护固定工具总数。可选插件和功能增量会改变数量；注册与参数覆盖不等于工具已完成真实交互或画面验收。
```sh
python3 scripts/mcp_client.py --server build/ninja-engine-release/mcp_app \
  --project projects/showcase --runtime-dir build/showcase-runtime list
```

输出直接来自当前二进制，包含完整 `inputSchema`。可选插件关闭后工具数量会变化；不要在 AI 客户端硬编码这份数量或假设全部工具存在。`engine.script_tooling` 对照实际装配检查工具集合及缺失插件行为，`engine.mcp_registry` 检查参数模型；执行成功、资源发布完成和画面正确仍须分别验证。

## 已有入口与缺口

| 功能 | 已有 MCP 入口 | 需要补齐或说明的边界 |
| --- | --- | --- |
| 项目、资源、启停 | `build_resources`、`sync_assets`、`start_project`、`stop_project`、`set_hot_reload`、`get_status` | `execute_project_script` 在活动 Run 中执行临时 TS；`close_editor` 返回回执后延迟正常关闭 |
| 场景与节点 | `get_scene`、`create_node`、`set_transform`、`set_node_state`、`set_component_state`、`save_scene`、`set_active_camera` | 查询、基础创建及激活/可见状态已有；通用节点删除、父子关系、常驻属性尚无专用编辑工具，不能把建模节点操作当成通用节点编辑 |
| 预制体 | `create_prefab`、`create_prefab_from_model`、`instantiate_prefab`、`link_prefab`、`unlink_prefab`、`get_prefab_instance`、`set_prefab_override`、`revert_prefab_override` | 支持子树模板与模型关联模板、独立实例及字段覆盖；嵌套、变体与应用实例覆盖到模板尚未开放，操作见[制作流程](../../editor/prefabs.md) |
| 编辑事务 | `execute_batch`、`undo`、`redo`、`get_editor_receipt` | 只针对已登记的可撤销编辑；运行脚本、模拟输入、播放和冲量不属于可回滚事务 |
| 预览与截图 | `open_scene_preview`、`close_scene_preview`、`get_scene_preview`、`set_preview_camera`、`refresh_scene_preview`、`capture_frame`、`get_frame_capture` | 截图须查询实际完成；预览不启动项目玩法 |
| 环境与后处理 | `get_render_settings`、`set_render_settings`、`set_render_stats` | 已有环境光、IBL 强度、雾、曝光、AO、HDR/Bloom、方向光 CSM 和 SSR；大气天空参数归环境组件，太阳引用使用真实 NodeID |
| 运行渲染能力与诊断 | `get_render_features`、`set_render_feature`、`get/set_mesh_render_settings`、`get_render_submission_stats`、`draw_debug_lines` | 统一入口路由到 Camera 剔除、MeshRenderer 实例化/LOD、环境天空和材质 Hex；组件新能力默认关闭，五项诊断与统计仅通过 MCP；真实逐视图提交计数不等于 GPU 完成 |
| 静态模型 LOD 导入 | `get_mesh_lod_import`、`set_mesh_lod_import` | 源 meta 原子更新，正式管线生成减面级别；与每个 MeshRenderer 的运行选择分离 |
| 灯光与材质 | `set_light`、`set_camera`、`set_material_type`、`set_material_parameter`、`set_material_texture` | 局部灯阴影与 `set_material_blend_mode` 已补齐；水面数值和贴图继续使用具名材质参数，不新增水体专用参数框架 |
| 反射与间接光 | `bake_scene`、`get_scene_bake`、`cancel_scene_bake` | 已增加 `get/set_planar_reflection`、`get/set_light_probe_volume`、`get/set_mesh_lighting`；资源版本与烘焙身份参与撤销校验 |
| 建模 | `modeling_help/create/get/select/edit/delete/duplicate/combine/separate/save/load` | 通过 `modeling_help` 查询具体操作；不是每个几何运算都新增顶层工具 |
| 物理 | `set/get_collider`、`set/get_rigid_body`、`add_impulse` | 射线、扫掠、范围查询、接触事件没有专用 MCP；项目 TS 执行入口可复用已绑定的查询和事件测试 |
| 角色与动画 | `set/get_character_motor`、`set/get_character_controller`、`set/get_character_camera`、`set/get_character_visual`、`instantiate_model`、`set/get_animator`、动画播放/暂停/停止/定位/状态 | 配置与播放已有；输入和多帧玩法结果须通过正常运行链验证 |
| 音频与特效 | 音源/监听器配置查询、音频播放/暂停/停止/状态、`set/get_effect`、特效播放/暂停/停止/状态 | 请求接受不等于已听到声音或看见特效 |
| 地形 | `create_terrain_asset`、`set/get_terrain`、`terrain_brush`、`terrain_reset`、状态保存/加载、脱离体坍塌/运行时重置 | 碎片回收及后续状态查询可通过项目 TS；保存资源与临时破坏状态是不同操作 |
| 建筑破坏 | `create_destruction_asset`、`set/get_destruction`、`destruction_damage`、`destruction_refresh_support`、`destruction_apply_stress`、`destruction_reset` | 碎片回收和跨帧联动可通过项目 TS，不复制一份项目伤害逻辑 |
| 导航与避让 | 无专用导航 MCP 工具 | 原生/TS 能力已有；路径、代理和局部重建验证通过项目 TS，导航编辑器插件目前只提供组件编解码 |
| UI | 文档、控件树/状态、源文件读写、输入/取消与 click 共 11 个工具 | 独立 `editor/ui` 插件；模拟输入经过 RmlUi，临时 View 修改不改变 Model，源码保存与导入激活分别报告 |
| 暂停、输入、异步任务 | 无独立全量 MCP 镜像 | 测试项目用 TS 组合已有能力，保留真实生命周期；不逐个镜像所有底层函数 |

表内 `set/get_*`、斜线分隔的名称是阅读简写；实际名称和完整参数以 `list` 输出为准。

## 本次已批准并实现的接口

1. [项目 TS 执行](../runtime/class-diagrams/project-script-execution.md)：直接提交 TS，复用编辑器编译器，在活动 Run 中执行并返回 JSON；不要求项目预先注册每个操作。
2. [UI 编辑与交互](class-diagrams/ui-editor.md)：查询控件及实际状态，模拟指针、拖动、滚轮、键盘和文字输入，区分源编辑与临时交互。
3. [渲染 MCP 补齐](class-diagrams/render-mcp-tools.md)：沿现有组件、命令和撤销链补参数及缺少的组件入口。

以上接口已评审并实现。运行功能仍通过场景组件/材质属性控制，可选功能默认关闭；诊断开关只保留 MCP 临时状态。注册、原生定向测试与真实窗口验证分别记录，不能用模拟返回值宣称业务链路已经通过。

## 验证要求

- 注册与 schema：启用时存在、禁用时消失；错误类型、未知字段、无效枚举和过期身份返回明确失败。
- 命令与持久化：编辑、撤销/重做、保存、重新导入后值一致；跨场景旧引用拒绝；失败不留下半个组件。
- 项目脚本：真实 TS 编译诊断、同一活动 VM、JSON 结果、停止/重载后的代次拒绝；执行失败不伪称已撤销副作用。
- UI：实际 RmlUi 输入到 Controller 的链路，检查焦点、拖动、滚动、文字、遮挡、隐藏/禁用与取消；不直接调用回调冒充操作成功。
- 范例：测试目的、步骤、预期结果、当前状态归项目维护；MCP 不写死 showcase 的场景或用例。

## 验证入口与记录

- MCP 注册、参数与协议入口：`engine.mcp_registry`、`engine.script_tooling`。实际工具集合依据本次二进制及插件选择。
- UI 源编辑、查询与输入入口：独立 `editor/ui` 插件及其[类图](class-diagrams/ui-editor.md)。
- 渲染运行属性与诊断入口：[统一入口契约](render-feature-controls.md)。
- 项目真实 UI/MCP 操作脚本与范围：[范例操作](../../editor/showcase-coverage.md)。
- 已执行的平台、通过结果、历史失败与证据：[集中验证记录](../../testing.md#mcp-与公开范例整理2026-11-03)、[上游渲染验收](../../testing.md#上游渲染能力与统一-mcp-功能验收2026-11-04)。

工具回执完成、资源导入激活、项目回调执行与最终画面正确分别核验。Release 用于功能和实际运行验收；Debug 只要求受影响目标编译成功。
