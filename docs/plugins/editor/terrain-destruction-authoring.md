# 地形与建筑破坏制作方案

地形导入、组件编解码、MCP、撤销/重做、建筑预破碎及 showcase 实验场已实现。制作工具保持同步准备和原子发布；运行时已接入地基联动、撞击反馈、应力、土石塌方、导航和连续火箭测试。整体目标见[运行时方案](../runtime/terrain-destruction.md)，采用范围与平台限制见[技术研究](../runtime/terrain-destruction-feasibility.md)，当前性能与待优化项见[性能采样](../../runtime/performance.md)。

公开场景使用 `projects/showcase/` 的自制地形、墙柱和分层塔试件，复用现有菜单、角色与效果；引擎回归按所属模块放在 `tests/`，项目交互检查留在项目内，不复制测试项目或引入商业资源。

生成、笔刷和预破碎同步返回完成或失败，保留准备与提交的职责边界。后台准备留待后续，既有 MCP 请求回执与编辑器命令通道继续沿用。

## 编辑器职责

`plugins/editor/terrain/` 已负责地形制作、导入与编辑；`plugins/editor/destruction/` 已负责官方预破碎、破坏源导入、组件编解码、设置/状态撤销和 MCP。地形插件独立 CMake 清单依赖 `runtime/terrain`，登记自身导入器、组件编解码和编辑命令；通过已有 MCP 扩展入口提供工具，编辑器基础不链接具体插件。地形契约见[编辑器类图](class-diagrams/terrain-editor.md)；建筑见[Authoring](class-diagrams/destruction-authoring.md)与[编辑器事务](class-diagrams/destruction-editor.md)。

预览、生成、撤销等行为由编辑器驱动运行时通用能力，不向运行时组件加入编辑器模式、撤销栈或项目扫描策略。MCP 只转发模块提供的命令，不维护另一套生成和破坏算法。

## 初始地形制作

- 已实现 `.terrain` TOML 参数化生成：`cells_x/y/z`、`voxel_size`、`chunk_cells`、`origin`、`height`、`amplitude`、`frequency`、`seed` 与 `[[cavities]]` 的 `center/radius`。当前为平面/正弦起伏与球形洞穴，FastNoise2 和高度图导入尚未接入。
- `.terrain.meta` 保留唯一资源身份，导入器按源数据和 schema/生成器指纹增量生成 `cache/gen/<GUID>`；运行时加载预生成密度，不依赖编辑器解析器。
- 已实现节点局部坐标下的球形挖掘、填充、重置及保存/恢复。脚本直接笔刷会复用块缓存并局部重建；编辑器/MCP 使用候选 ETRS 快照和 `TerrainStateEdit`，当前 `restore` 重新初始化体积，因此笔刷、撤销/重做、加载和重置均重建全部块。渲染仍合并为单 Mesh 后整体上传；静态碰撞使用分段几何，直接笔刷可复用未变叶子和复合树分组。恢复路径的全量准备成本仍需单独测量，不能套用局部笔刷预算。见[碰撞方案](../runtime/terrain-collision.md)。
- 地表与洞壁可设置顶点色及 UV 缩放，材质引用使用通用资源路径和 Shader 参数；平滑、材质层笔刷和更丰富的纹理映射尚未实现。
- 初始地图与运行修改分离；无限地图、运行时按需生成和后台队列均留待后续。

## 建筑破坏资源制作

- `.destruction` TOML 描述 `size`、`fracture = "slices" | "voronoi"`、`slices_x/y/z`（每轴最终份数）、`sites`、`seed`、`bond_health` 与 `anchor_height`。原始盒体底部位于局部 y=0，节点变换决定场景位置；未知字段、超限或无效值明确失败。
- 切割、三角体积积分和 EXACT 三角面接触连接使用 Blast 5.1 官方 ExtAuthoring；最多 256 个块，尺寸各轴不超过 1000，连接强度大于 0 且不超过 1e6。当前只支持参数盒体，不将生成试件宣称为任意 FBX、空心建筑或实时切割工具。
- 生成 `DestructionAssetData` 保存片段几何、体积、质心、初始锚点与连接；初始锚点强度使用 `bond_health`。不发布 Blast 平台内存布局，材质由场景组件显式引用。
- `DestructionImporter` 参与现有增量管线和原子资源发布；`DestructionCodec` 维护 `type = "destruction"` 与 destruction/material GUID、密度、摩擦及恢复系数。运行时不需要 TOML、Authoring 或 Boost。
- 动态组使用 Jolt 官方凸包/复合形状，一个组可保留多个相连碎块。首版仅允许根节点与正等比缩放；显示和碰撞节点都是可重建的瞬态对象，不进入场景持久数据。
- 切面独立材质、碎块层级和任意模型输入尚未实现。运行时应力、地基采样和撞击反馈已接入，使用相同破坏资源；制作工具不承担运行时求解。

## 参数、导入与发布

可编辑描述采用现有 TOML 约定；源身份、子资源映射和导入配置放相邻 `.meta`。生成参数、工具版本与源数据参与增量指纹，未改变的输入不重复转换。

体积与破坏数据生成独立 GUID 资源，只声明真实依赖；破坏资源内部的片段几何及连接是整个可破坏体的必要数据，并非包含所有同源资源的虚拟包。普通 Mesh 等其他资源仍保持原有独立身份。虚拟分组用于编辑器组织，不成为必须整体加载的运行包；打包只收集实际使用的内容。缓存继续使用 `cache/gen/<完整GUID>`，不产生另一套命名或打包路径。

管线负责扫描、增量、任务与原子发布，具体导入器负责识别、参数、转换、依赖和工具指纹。生成失败不替换旧有效资源。运行修改不改原始源资源或导入缓存。地形已有独立存档入口，建筑当前提供运行状态快照及编辑器撤销，独立游戏存档槽 API 留待后续。

地形与建筑的状态和配置编辑通过现有事务支持撤销、重做；重做恢复相同结果，随机生成使用明确种子。后台生成取消属于后续异步阶段。增量更新保留稳定资源身份，不能因每次重建任意更换 GUID 导致引用失效。

## 已实现的地形工具

MCP 与通用编辑器命令同名，节点工具沿用已有场景/节点身份参数：

| 工具 | 功能 |
| --- | --- |
| `create_terrain_asset` | 在 assets 下创建 `.terrain` 源与唯一 meta，不覆盖已有文件；之后用已有刷新/同步命令导入 |
| `set_terrain` / `get_terrain` | 设置或读取地形资源、材质、碰撞、地表/洞壁颜色及 UV 缩放，读取 ready/revision/块数/三角形数；设置可撤销 |
| `terrain_brush` | 同步球形挖掘或填充，坐标为节点局部坐标；完成后显示与碰撞一起生效，可撤销 |
| `terrain_reset` | 恢复基础资源体积，可撤销 |
| `terrain_save_state` | 将完整当前体积与基础 GUID/hash 保存到 `cache/terrain-states/` 下相对 `.terrainstate` 文件 |
| `terrain_load_state` | 检查基础身份后恢复快照，可撤销，失败保留当前状态 |

笔刷、恢复与设置编辑接入现有事务；撤销/重做检查预期状态，避免静默覆盖之后的其他修改。`TerrainStateEdit` 通过 `restore` 应用候选 ETRS 快照，当前会丢弃旧块缓存并全块重建；MCP 笔刷不能视为脚本直接 `apply_sphere` 的局部缓存路径。创建源文件及保存快照属于文件写入，不声称可以通过场景 undo 删除文件。快照目前保存整个体积，不是只保存修改块。

脚本接口由 `TerrainComponent` 提供 `DigSphere`、`FillSphere`、`Reset`、`Save`、`Load` 和只读状态，参见[脚本类图](../runtime/class-diagrams/terrain-script-api.md)。脚本存档位于 App 持久目录下 `terrain/<slot>.bin`，槽名仅允许 1–48 个字母、数字、下划线或连字符；与编辑器 `cache/terrain-states` 路径分开。

showcase 菜单第六项 `Terrain laboratory` 进入 `scenes/terrain.scene`，支持挖隧道、爆炸、填充、保存/加载、重置、俯瞰/洞口观察和第三人称行走，继续复用原有角色。第七项 `Destruction laboratory` 进入 `scenes/destruction.scene`，使用自制 48 块塔、24 块 Voronoi 墙和 8 段柱，提供固定损伤区域按钮、重置、统计及两台固定观察镜头。

首版同步计算与异步 MCP 传输是不同概念，不因使用 MCP 就引入后台生成框架。后续异步生成返回任务身份，通过查询取得结果；各阶段调用超时都不直接重复提交有副作用的操作。未启用插件时不登记工具，也不把编辑器工具放入设备运行包。

## 建筑破坏工具

| 工具 | 功能 |
| --- | --- |
| `create_destruction_asset` | 在 assets 内创建经过官方 Authoring 校验的 `.destruction` 源与唯一 meta，不覆盖已有文件；随后沿用现有导入命令 |
| `set_destruction` / `get_destruction` | 设置资源路径、材质、密度、摩擦和恢复系数，查询 ready/revision/chunks/groups/dynamicGroups；场景保存 GUID |
| `destruction_damage` | 世界坐标球形损伤，支持非负 damage 与可选 impulse，更新 Blast 组和 Jolt 碰撞；可撤销 |
| `destruction_reset` | 恢复初始图和初始姿态；可撤销 |

状态快照含基础 GUID/hash、连接血量、每组 chunks、世界 Transform 和线/角速度。`DestructionStateEdit` 首次 apply 执行损伤/重置并保存结果，后续 undo/redo 恢复同一快照。状态操作不标记场景初始设置为已修改；设置编辑保存前后设置和完整状态，避免更换资源后撤销丢失旧损伤。

编辑记录使用场景身份、稳定节点、组件设置与共享预期 revision 验证。连续编辑共用游标并在成功应用/撤销后更新，支持多层 undo/redo；普通物理位移不改变 revision，脚本外部损伤则拒绝由旧历史覆盖。新资源输入失败时保留原有效资源；后台线程和任务取消不属于当前实现。

脚本 `DestructionComponent.DamageSphere(x,y,z,radius,damage,impulse=0)` 与 `Reset()` 使用相同运行时系统。当前没有独立建筑 Save/Load 槽位；地基联动由项目调用已有支撑更新接口组织。MCP schema 使用现有注册器支持的 `minimum`，半径和密度的严格正数要求由业务校验落实。

## 开发前约束

先核实开源版本、许可、依赖和平台可用性，再更新所属层级类图并实现。商业包只在本地参考，不提交其源码、转换片段或付费素材。公开测试使用可提交的自制或许可允许的资源；项目专有内容不迁移到其他项目。

旧阶段日志保留在 Git 和本地构建目录；当前平台验证范围以[测试文档](../../testing.md#验证范围与限制)为准。修改制作链路时只选择对应导入、命令或发布用例，不运行全套地形/破坏场景。

建筑制作的采用范围和平台边界见[技术研究](../runtime/terrain-destruction-feasibility.md)，不将资源清单登记、依赖编译或独立 Graph 探针视为全平台完成。
