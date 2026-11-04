# 渲染功能统一入口

运行开关放对应组件属性，新增组件能力默认关闭；调试显示、提交统计和调试线只通过 MCP 访问，不建立独立全局优化状态。工具可用性以实际构建的 tools/list 为准；运行和画面验收单独记录，不把登记或语法编译当作通过。既有编辑功能见[渲染 MCP](class-diagrams/render-mcp-tools.md)，底层契约见[渲染能力与调试控制](../../runtime/class-diagrams/render-feature-controls.md)。

## 现有链路与约束

- `RenderCommands` 已借用 `RenderSystem/WorldSystem`，由 `EditorCommands` 登记；`register_render_tools()` 只声明 schema、转发命令与原回执。继续复用，不新增 FeatureManager 或 MCP 私有开关缓存。
- `set_render_settings` 已把环境、后处理和灯写入真实组件，支持撤销及保存；普通材质的 `set_material_parameter` 已经按名字/明确类型更新。新统一入口负责选择正确入口，不另造场景级全局覆盖。
- 现有 `set_render_stats` 保留为临时诊断，同一状态也出现在统一入口。现有截图仍使用 `capture_frame/get_frame_capture`；MCP 接受绘制或设置请求不等于 GPU 已完成。
- 主菜单、编辑器属性页和 AI 使用同一功能名称/域/真实状态。TS 仍通过领域原生 API 操作，不通过 JSON 编辑器命令反向驱动核心。

## 工具清单

| 工具 | 参数 | 行为 |
| --- | --- | --- |
| `get_render_features` | 可选 `sceneId/nodeId` 成对提供；`slot` 必须同时有该身份对 | 总是返回 renderer 域；有活动场景时返回环境域；明确选择材质槽时追加该材质域。返回支持、请求、生效、原因及保存归属 |
| `set_render_feature` | 必需 `feature/enabled`；运行组件/材质域要求明确目标身份 | 按归属转发；组件属性与当前设备能力分离，可在未初始化或不支持时保存 true。错误身份/参数失败，临时 wireframe 在硬件不支持时拒绝开启 |
| `get_mesh_render_settings` | `sceneId/nodeId` | 查询指定 MeshRenderer 的两个运行属性 |
| `set_mesh_render_settings` | `sceneId/nodeId`，至少一项 `staticInstancingEnabled/staticLodEnabled` bool | 原子编辑指定属性，支持撤销/重做和正常保存 |
| `get_render_submission_stats` | 无参数 | 返回本模块最近提交帧的逐视图 scene mesh 计数，值来自 `RenderSystem::submission_stats()`；不包含 UI、不冒充 bgfx/GPU 完成统计 |
| `draw_debug_lines` | `lines`，可选 `lifetimeFrames`（1..600，默认 1） | 经主线程命令提交受控通用线段；返回接受条数与提交帧号，不声称已显示 |

LOD 离线配置另由资源域提供 `get_mesh_lod_import` / `set_mesh_lod_import`，参数、meta 原子更新及导入回执见[静态模型 LOD](../../editor/static-model-lod.md)。这是资源生成入口，运行中的 `static_lod` 开关仍出现在本页统一入口；MCP 不在 RenderSystem 内读写源文件。

不增加 `start_benchmark` 作业框架。公开 showcase 渲染对照用例通过现有 `start_project/execute_project_script` 驱动，在 TS 里选择相机并编辑对象组件属性，再由 MCP 查询真实渲染状态，GPU 截图继续走已有工具。

### 统一名称与路由

| `feature` | 域及最终写入 | 目标及支持条件 |
| --- | --- | --- |
| `frustum_culling` | camera，`CameraData.frustum_culling_enabled` | 必须 sceneId/nodeId 对应 Camera；转发原 set_camera.frustumCullingEnabled，可保存/撤销 |
| `static_instancing` | mesh，`MeshRendererData.static_instancing_enabled` | 必须 sceneId/nodeId 对应 MeshRenderer；属性 true 不保证资源/材质具备合批资格 |
| `static_lod` | mesh，`MeshRendererData.static_lod_enabled` | 必须 sceneId/nodeId；仅选择已生成的 LOD，不改变源 meta |
| `wireframe` | renderer 临时诊断，`set_debug_feature` | 首期实现支持的主视图几何；不污染其他通道 |
| `debug_draw` | renderer 临时诊断，同上 | 只控制调用方提供的临时通用线数据 |
| `mesh_bounds` | renderer 临时诊断，同上 | 自动绘制真实边界，不依赖额外开启 `debug_draw` |
| `camera_frusta` | renderer 临时诊断，同上 | 自动绘制已发布相机视锥，范围见底层能力图 |
| `stats` | renderer，复用现有 stats 状态 | 与原有 `set_render_stats` 双向一致 |
| `atmosphere` | scene，指定 `EnvironmentComponent` | 明确 `sceneId/nodeId`；组件必须有效。精确字段跟随天空设计，不存在组件时明确失败 |
| `hex_tiling` | material，指定 `Material` 的具名开关参数 | 明确 `sceneId/nodeId/slot`，只接受声明该参数且数值类型匹配的 Shader。参数名/数值契约由 Hex 材质定义确定 |

`frame_view_uniforms` 仅作为 renderer 查询里的只读 `implementation` 能力返回 `active` 与实际频率覆盖信息，`set_render_feature` 对该名明确拒绝。UniformFreq 是已编译的 Program/Uniform 契约，不做没有实际资源重建的假开关。

统一入口第一期只处理功能布尔开关；数值配置放现有所属编辑组：天空参数随环境组件、Hex 尺度/混合随材质参数、LOD 导入随资源 meta。不会让 `set_render_feature` 变成任意 JSON 参数写入接口。

### 返回示意

```json
{
  "renderer": {
    "scope": "world_summary",
    "features": {
      "static_instancing": {
        "requested": true,
        "supported": true,
        "effective": true,
        "reason": "none",
        "persistent": false
      }
    },
    "implementation": {
      "frame_view_uniforms": {"active": true, "mutable": false}
    }
  },
  "scene": {
    "sceneId": "12",
    "nodeId": "44",
    "features": {
      "atmosphere": {"requested": false, "supported": true, "effective": false, "reason": "none", "persistent": true}
    }
  },
  "material": {
    "sceneId": "12",
    "nodeId": "75",
    "slot": 0,
    "features": {
      "hex_tiling": {"requested": false, "supported": false, "ready": false, "effective": null, "reason": "shader_unsupported", "persistent": true, "scope": "material_parameter"}
    }
  }
}
```

示意仅展示返回结构，不是某次实际运行回执。`supported=false` 必须伴随实际原因；设备未就绪、场景未选择、组件缺失、Shader 不声明等分别可读。查询不会为了给出“已支持”而添加组件、修改材质、加载新场景或创建资源。

## 逐对象属性与只读汇总

`get_render_features.renderer.features` 是当前世界请求与最近提交效果的只读汇总，明确标注 `scope: world_summary`，覆盖当前世界所有已发布且可渲染的场景（含常驻场景），与实际提交范围一致，不能作为全局写入状态。指定 Camera 时额外返回该 Camera 的 `frustumCullingEnabled`；指定 MeshRenderer 时返回其 `staticInstancingEnabled/staticLodEnabled` 真实属性，标注 `scope: component`。不将其他对象产生的 instance batch 报成选中对象的 effective。

`get_mesh_render_settings` / `set_mesh_render_settings` 只查询和编辑上述两个 MeshRenderer 属性；setter 至少给一个 bool。两项保存在唯一生成数据列；编辑/撤销只恢复对应属性，不能通过整组件 restore 重建网格、丢失材质覆盖或瞬态变形。资源 LOD 生成仍留在源 meta，不创建另一份场景 LOD 配置。

临时 debug 开关只要求 `feature/enabled`，拒绝 scene/material 身份；运行三项及天空、材质开关要求明确组件身份。统一工具仍是一处入口，状态由正确模块各自持有。

## 编辑、事务与生命周期

完整类关系由[渲染 MCP 类图](class-diagrams/render-mcp-tools.md)与[渲染功能类图](../../runtime/class-diagrams/render-feature-controls.md)维护，本页只说明命令路由。

- `RenderCommands` 继续只有借用，没有功能状态。统一名称的静态登记描述包含名称、归属和转发函数；不根据项目、场景名或材质文件名判断。
- 场景/材质修改复用现有 `EditorEdit`，标记未保存并可撤销/重做；仅 debug 诊断开关和调试线不写场景、不进入撤销历史；三项运行属性随所属组件正常保存。现有 `EditorCommands::prepare` 严格要求登记的 undoable 与是否返回 edit 一致，因此首选由 MCP 适配层按域将一项 `set_render_feature` 转发为一项正式命令：debug 使用瞬态 `set_render_debug_feature`，Camera 剔除使用可撤销 `set_camera`，MeshRenderer 两项策略使用可撤销 `set_mesh_render_settings`，scene 使用可撤销 `set_render_settings`，material 使用可撤销 `set_material_parameter`。每次只有一个正常命令回执和一次历史提交，不嵌套 `execute`，无需改变 EditorCommands 公开契约。
- 为避免跨进程“查目标再写”的竞态，天空路由给 `set_render_settings` 增加可选 `sceneId/nodeId` 校验参数：成对给出时必须精确匹配当前场景实际使用的环境组件，再执行同一领域 edit；不匹配或不存在直接失败。旧调用不传身份时保留既有活动环境编辑语义。Hex 路由沿现有材质槽身份校验。
- 原子持久批量继续使用 `execute_batch` 与所属领域的 edit 命令；临时 renderer 开关不伪装为可撤销场景数据。
- 场景/材质请求必须提供完整十进制字符串身份，不接受 OS 指针、GPU handle、过期节点或槽。没有 `slot` 不猜测第一个材质；没有环境目标不把天空修改应用到其他场景。
- renderer setter 接受后返回真实 requested/support 状态及当前提交帧号；以后查询从实际已提交帧获得 effective。`get_render_submission_stats` 回执同时标注当前已提交帧号，MCP 等待新帧再捕获，禁止用固定 sleep 假定已应用。逐视图计数不附会 GPU 完成时间，CPU/GPU 计时继续由现有 Tracy/bgfx 性能链独立获取并说明来源。
- 多对象策略对照由 TS 修改各 MeshRenderer 属性，相机剔除修改 Camera 属性；本期统一 MCP setter 不承诺跨 renderer/scene/material 的复合原子事务，避免伪造回滚。
- 通用调试线每项包含两个三维端点、`color` 为引擎 RGBA 无符号整数、`depthTest` 布尔；未知字段、非有限值、超预算整批失败。受当前场景身份约束，切场景即清理。返回 accepted 不等于截图里已显示，调用方继续正常提交帧。

## TS 门面

```typescript
Camera.SetFrustumCulling(enabled: boolean): boolean;
Camera.frustumCullingEnabled: boolean;
MeshRenderer.SetStaticInstancing(enabled: boolean): boolean;
MeshRenderer.staticInstancingEnabled: boolean;
MeshRenderer.SetStaticLod(enabled: boolean): boolean;
MeshRenderer.staticLodEnabled: boolean;
```

上述三个运行属性默认 false，绑定同步 `include/engine.d.ts`；项目不通过系统对象读取统计或提交调试绘制。句柄失效时 setter=false，getter 返回安全默认值。组件属性保存不要求 GPU 初始化。天空使用现有环境组件绑定，Hex 继续 `Material.SetFloat`；诊断能力和真实提交统计仅由 MCP 查询。

编辑器的统一入口是同一组正式命令与 MCP 工具；当前 EditorSystem 只有 ImGuiContext 生命周期，没有完整属性面板及 ImGui 帧绘制链路，本轮未另建编辑器 GUI。公开 showcase 的独立渲染用例提供可操作功能面板，调用同一 TS 原生 API，显示实际组件状态；设备支持、生效原因和逐视图统计由 MCP 查询。天空和材质仍明确属于场景/选中材质，不在面板保存第二份持久数据。

## 必要验证

- `tools/list` 登记完整 schema；未知名称、错误布尔、失效身份、材质未声明、没有设备等返回明确失败，原状态不变。
- 两入口 stats 开关一致；TS/MCP 操作后原生查询一致；切场景恢复场景环境，无上次场景覆盖值。
- 统一入口的场景/材质修改真正进入撤销、重做、保存、重导入；组件运行属性产生正常场景脏状态；只有临时 debug 变化不产生脏状态。
- Debug 只编译；Release 执行 MCP 注册、编辑命令/脚本生命周期、真实窗口及截图验证。独立公开用例固定对象与镜头，使用真实提交计数检验路径，不收集性能分数或开展无关模块优化。
