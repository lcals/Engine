# 建模插件

建模用于 AI 通过 C++ 和 MCP 搭建基础场景：生成基本体、组合结构、调整几何、UV 与材质，保存为普通运行资源。复杂模型由 DCC 制作，再通过资源导入流程使用；本模块不以完整复刻 ProBuilder 或 UModeler 为目标，不提供图形编辑界面。

## 职责与现有能力

| 层 | 职责 |
| --- | --- |
| 建模几何 `engine::modeling` | 纯数据生成、校验、拓扑、UV、法线和网格编译，属于编辑器插件 |
| 建模业务 `engine::modeling_editor` | 借用 WorldSystem，组织模型、选择、事务、撤销和源文件 |
| MCP 适配 `engine::modeling_mcp_tools` | 注册参数 Schema，转发命令及实际结果 |
| 资源导入 `engine::modeling_importer` | 编译权威编辑源，由宿主注入资源管线 |
| 运行时 | 只加载编译后的 Mesh、材质及场景，不依赖建模插件 |

公开计算接口位于 `engine::plugins::modeling::mesh`，按职责分文件；代码已经使用命名空间函数。编辑器基础能力不链接具体建模插件，场景保存通过宿主注入网格导出回调。入口见[类图索引](../../class-diagrams/index.md)、[建模计算](class-diagrams/editable-mesh.md)和[建模编辑业务](class-diagrams/modeling-commands.md)。

基础场景搭建能力如下。

| 能力 | 入口 |
| --- | --- |
| 方块、平面、圆柱、圆锥、球、环面、三棱柱、楼梯、拱体、管、门框、弧形楼梯、精灵平面、细分球 | `modeling_create` |
| 带洞多边形、开放路径扫掠、旋转体 | `modeling_create` |
| 点边面变换、挤出、内插、拆分、焊接、桥接、封洞、切割、布尔和受限倒角 | `modeling_select / modeling_edit` |
| UV 投影、变换、展开、材质槽、颜色、平滑组 | `modeling_edit` |
| 查询、复制、合并、分离、删除、保存和恢复 | `modeling_get / duplicate / combine / separate / delete / save / load` |
| 操作发现、事务和历史 | `modeling_help / execute_batch / undo / redo` |

## 参数与尺寸

| 能力 | 契约 |
| --- | --- |
| 基础 UV 密度 | 基本体按本地长度生成 UV，提供 `uvTilesPerUnit`；节点缩放不隐式重写源 UV |
| 精确创建参数 | 各形状只接受有效字段，帮助、Schema 和校验使用同一参数表 |
| 尺寸与边界 | 查询实际可见网格的本地／世界包围盒，供 AI 计算贴地、拼接与对齐 |
| 参数再编辑 | 保存生成参数；`modeling_rebuild` 在原节点重建，清空旧点面选择，保留变换和材质绑定，支持撤销 |

字段、默认值和 UV 规则只在[基本体类图](class-diagrams/mesh-primitives.md)维护；重建对几何编辑、身份及材质的影响只在[业务契约](class-diagrams/modeling-commands.md#按参数重建)维护。

接口和源格式只维护当前版本，生成参数直接使用 Schema 数据。建模能力全部在编辑器插件内，运行时只消费编译资源。

## 操作流程

1. 编辑器启动项目后，通过 `modeling_help` 查询操作，使用 `get_scene` 获取实际 `sceneId`。
2. `modeling_create` 返回稳定 `modelId`，`modeling_get` 获取当前 `nodeId` 和拓扑。删除再撤销可能重建节点，不能继续使用旧 `nodeId`。
3. `modeling_rebuild` 修改基本体尺寸并重新生成，`modeling_select / modeling_edit` 编辑点、边、面。重建会覆盖旧几何与表面编辑；改变拓扑后重新查询。面操作要求非空面选区或显式 `all=true`。
4. 节点位置、旋转、缩放和材质参数使用所属组件命令；建模与场景修改共用已有 `execute_batch / undo / redo`。
5. 按下节保存和导入资源。文件保存不加入几何编辑批次。

场景切换会清理旧模型身份、选择和撤销记录；宿主通过 `synchronize_scene` 从有效源资源恢复可编辑数据。纯几何函数在候选数据上工作，失败不发布部分结果。MCP 的职责见[建模 MCP](class-diagrams/modeling-mcp-tools.md)。

## 资源保存

`modeling_save({sceneId, modelId, path})` 保存 assets 内的单个 `.mesh` 源文件；`modeling_load` 恢复其中的可编辑数据。`save_scene({sceneId, path})` 由通用 ScenePersistence 保存完整场景、组件和资源引用，建模网格通过宿主回调导出。

- 持久模型唯一来源为 `include/schemas/runtime/modeling.fbs`，源文件的 `[modeling]` 使用 FlatBuffers 十六进制数据。
- 源文件只保存权威编辑数据，导入时生成渲染数组，详见[导入类图](class-diagrams/modeling-mesh-importer.md)。没有原始生成记录的模型可继续编辑，`primitive` 为 `null`，不能按参数重建。
- 资源管线负责唯一 meta、GUID、增量缓存及发布；生成物位于 `cache/gen/<GUID>`，运行包不包含选择、撤销历史或生成参数。
- 已加载且未覆盖的网格／材质保持原 GUID；动态网格及覆盖材质写入场景同名资源目录。未改变文件不重写，已有 GUID 保留。
- 保存不改变 Gameplay 入口。运行项目需要重新部署时使用 `save_scene → stop_project → sync_assets → start_project`；单个模型保存不自动把它加入场景源文件。
- 场景保存失败回滚本批源文件，外部修改冲突拒绝覆盖。保存本身不清空编辑历史，后续仍可撤销、重做后再保存。

## 现有算法边界

- 多段倒角支持闭合凸体的有效折边；凹体、开放边和共面三角化内部边不在其输入范围。基础体的小倒角可用现有实现，复杂拓扑由 DCC 处理。
- 组内插要求同向共面区域；逐面内插遇到含洞面需要先拆分。
- 布尔和封口裁切要求有效闭合实体；无封口裁切允许开放网格，不隐式修复自交。
- 扫掠支持开放路径；旋转体以局部 Y 轴为轴。不提供闭环路径标架。
- 编辑 UV 为一个通道，支持自动展开和接缝；多 UV 制作与完整 UV 岛编辑交给 DCC。
- 从渲染 Mesh 恢复编辑网格会依据共享边恢复平滑组并重算法线，不逐值保留自定义法线。
- 持续镜像是编辑器会话状态，保存时烘焙结果；源模型需处于镜像平面同一半空间。
- 删除建模对象只接受根级、无子节点且只有 Transform 与 MeshRenderer 的节点；不能连带删除其他业务组件。

## 验证与来源

验证遵循[测试约定](../../testing.md)，复用建模 core、geometry、commands、pipeline 及必要的 scene persistence 用例。C++ 改动只构建受影响 Debug / Release 目标，功能测试使用 Release；只有实际改变 MCP 进程或 GPU 链路时才运行相应集成用例，不建立新测试程序或全排列场景。

- [Earcut](https://github.com/mapbox/earcut.hpp)：多边形与孔洞三角化，ISC。
- [Manifold](https://github.com/elalish/manifold)：布尔和裁切，Apache-2.0；其 Clipper2 依赖使用 BSL-1.0。
- [xatlas](https://github.com/jpcy/xatlas)：UV 图集展开，MIT。

依赖版本由 vcpkg 或固定 Git 子模块维护。基础拓扑为项目 C++ 实现；其他工具作为功能参考，不复制受限实现。
