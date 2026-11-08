# MCP 工具能力索引

[MCP 用法](mcp.md) · [公开范例测试](../../testing.md) · [类图索引](../../class-diagrams/index.md)

## 查询工具

通过当前构建的标准 `tools/list` 核对实际装配的工具、描述与参数，不在文档维护固定工具总数。可选插件和功能增量会改变数量；注册与参数覆盖不等于工具已完成真实交互或画面验收。
```sh
python3 scripts/mcp_client.py --server build/ninja-engine-release/mcp_app \
  --project projects/showcase --runtime-dir build/showcase-runtime list
```

输出直接来自当前二进制，包含完整 `inputSchema`。可选插件关闭后工具数量会变化；不要在 AI 客户端硬编码这份数量或假设全部工具存在。`engine.script_tooling` 对照实际装配检查工具集合及缺失插件行为，`engine.mcp_registry` 检查参数模型；执行成功、资源发布完成和画面正确仍须分别验证。

## 模块与范围

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

## 相关契约

- [项目 TS 执行](../runtime/class-diagrams/project-script-execution.md)：编辑器编译 TS，在活动 Run 中执行并返回 JSON；失败不撤销已发生的项目副作用。
- [UI 编辑与交互](class-diagrams/ui-editor.md)：源编辑、临时控件状态与真实输入分别报告，控件引用随文档代次失效。
- [渲染 MCP](class-diagrams/render-mcp-tools.md)：沿组件、命令和撤销链操作，临时诊断状态不写入场景。

工具调用完成、资源发布、项目回调和画面结果属于不同完成状态。参数与协议使用现有 `engine.mcp_registry`、`engine.script_tooling` 用例；真实交互按[测试文档](../../testing.md)选择相关范围。
