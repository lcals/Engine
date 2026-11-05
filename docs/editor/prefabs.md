# 预制体制作与场景使用

[数据类图](../runtime/class-diagrams/prefab.md) · [编辑器类图](class-diagrams/prefab.md) · [资源管线](asset-pipeline.md)

预制体是一棵可重复实例化的普通 Node/Component 子树。各实例拥有独立组件状态；改实例不改模板或其他实例。模板再次导入时，未覆盖字段使用新值，明确覆盖过的字段保留。运行时模拟不自动成为编辑覆盖。

## 制作流程

美术资源先创建 `.prefab`，再通过预制体放入场景：

1. FBX 等源模型经过正常 GUID/meta 导入，得到 Model、Mesh、Material 等独立资源。
2. 用 `create_prefab_from_model` 将已导入的 Model 创建为关联模板；模型更新会重建依赖它的模板和场景。也可将建模生成或手工组合的节点子树，用 `create_prefab` 保存为直接模板。
3. 用 `instantiate_prefab` 放入场景。编辑和保存场景时保留来源及属性覆盖，不把展开结果写成脱离模板的副本。
4. TS 动态生成角色、道具时调用 `scene.InstantiatePrefab("prefabs/characters/player.prefab", "Player")`；场景持有返回的节点，销毁根节点同时释放其子树。

源文件使用 TOML，`.meta` 保留独立 GUID；运行包使用编译后的 FlatBuffers，不读取 TOML。预制体模板是构建依赖；已展开场景的运行依赖只包含其实际使用的资源。脚本动态实例化的预制体需加入项目 `editor-resource-build.toml` 的入口，之后 Mesh/Material 等依赖自动收集。

## 编辑器与 MCP

| 命令 | 参数与作用 |
| --- | --- |
| `create_prefab_from_model` | `model, path`；从资源路径创建新的关联 `.prefab` |
| `create_prefab` | `sceneId, nodeId, path`；导出子树，保留动态 Mesh 与材质，不改变原树关联 |
| `instantiate_prefab` | `sceneId, path, name`；创建独立实例，支持撤销 |
| `link_prefab` | `sceneId, nodeId, path`；把刚导出模板的原子树关联回模板，要求原节点稳定身份匹配 |
| `get_prefab_instance` | `sceneId, nodeId`；查询来源、源节点 ID 与覆盖字段 |
| `set_prefab_override` | `sceneId, nodeId, fields, componentType?`；将当前值明确标记为覆盖 |
| `revert_prefab_override` | 同上；清除指定覆盖，恢复最新模板值 |
| `unlink_prefab` | `sceneId, nodeId`；在实例根解除关联，保留当前普通节点树 |

节点和场景 ID 取自 `get_scene`。组件类型使用返回的 `componentType`，例如 `TransformData`；`fields` 使用组件源描述的属性名，例如 `position`、`enabled`、`material`。向量、数组整体算一个属性。常规 `set_transform`、`set_node_state` 等编辑命令自动登记被请求字段；设置为相同值、`0`、`false` 仍然是覆盖。撤销/重做同步恢复属性和覆盖记录。

例如恢复实例某节点的模板位置：

```json
{"sceneId":"12","nodeId":"42","componentType":"TransformData","fields":["position"]}
```

将上述参数传给 `revert_prefab_override`。不改变其他字段的覆盖。

## 范围

- 模板内节点 ID、实例场景 local ID 与运行时节点句柄严格分开；FBX 的稳定源路径映射不会因兄弟节点排序而改变。已经删除的身份保留为墓碑，不分配给新节点。
- 模板子树外的节点引用不自动猜测。导出无法封闭的引用会失败；场景实例的明确引用覆盖可以指向该场景其他节点。
- 源格式支持组件增删与节点删除的明确记录。编辑命令仅开放已经适配的操作；未支持的结构操作明确拒绝，可先解除关联再编辑。
- 材质绑定按整项资源覆盖。参数或贴图编辑保存为独立材质资源，不提供跨模板逐 Shader 参数的继承合并。
- 首版不包含嵌套预制体、预制体变体和“应用实例覆盖到模板”命令。模板通过明确编辑其 `.prefab` 源或关联模型更新；普通保存场景始终不会回写模板。
- ECS 迁移不在本次改动范围，仍使用现有 Node/Component 与各业务系统。

公开范例的场景环境、交互结构已移到 `projects/showcase/assets/prefabs/scenes/`，角色模板位于 `prefabs/characters/player.prefab`。原场景入口、节点名称、组件和变换保持；静态平铺环境可增加恒等变换包装根。现有美术生成脚本重新产出场景后，也应通过上述命令制作/关联模板，不能用旧展开源覆盖已维护的预制体场景。
