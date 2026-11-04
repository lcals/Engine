# 地形与建筑破坏联动

本页维护跨插件协作和正确性边界。单模块用法分别见[体积地形](terrain.md)、[建筑破坏](destruction.md)，资源制作见[地形与破坏制作](../editor/terrain-destruction-authoring.md)。历史选型、固定来源和独立探针保留在[技术研究](terrain-destruction-feasibility.md)，实际验证结果和平台限制见[测试文档](../../testing.md)。

## 职责与依赖

| 位置 | 职责 |
| --- | --- |
| 运行时核心 | 通用瞬态网格、碰撞数据安全替换、查询、接触快照、导航及渲染；不识别地形或建筑业务 |
| `plugins/runtime/terrain/` | 密度体积、分块球形修改、表面提取、静态体积快照、显式土石连通提取与回收 |
| `plugins/runtime/destruction/` | Blast 连接损伤、支撑图拆分、重力应力、瞬态组与状态快照 |
| `plugins/editor/terrain/` | `.terrain` 源导入、组件编解码、可撤销编辑与 MCP |
| `plugins/editor/destruction/` | Authoring 预破碎、`.destruction` 导入、组件编解码、可撤销编辑与 MCP |
| 项目脚本或宿主调用方 | 命中与伤害规则、修改顺序、支撑刷新、导航脏区、表现及调用预算 |

两个运行时插件不相互 include 或链接，由调用方使用普通节点组件传递明确结果。项目只经 `TerrainComponent` / `DestructionComponent` 操作运行属性与请求；核心与编辑器基础不引用具体插件。宿主按各插件 CMake 清单装配，正式运行包不包含 MCP、Authoring、Boost 或 PhysX。

地形复用固定 Transvoxel regular-cell 查找表及 Voxel Tools 连通/浮岛算法；建筑复用 Blast low-level、ExtShaders、ExtAuthoring 和 ExtStress 原标量求解器；运动与碰撞复用 Jolt。固定版本、许可、ARM 兼容补丁和未采用的候选见[核对基线](terrain-destruction-feasibility.md#核对基线)，不把第三方宣称的平台支持当作 Engine 验收。

## 候选准备与发布

同步接口保留即时完成语义。异步球形笔刷和建筑损伤复用共享 oneTBB 任务域，后台只准备独占候选，不修改活动场景、GPU 或 Jolt 世界；协调线程校验组件身份与版本后有界发布。队列、并发与提交预算见[运行并行](../../runtime/parallelism.md)。

候选全部成功后才替换匹配版本的显示与碰撞；失败保留旧有效状态。组件移除、场景卸载、Reset 或同步修改使旧候选失效；常驻源节点仍按原身份管理。首次生成、Reset、连通塌方、应力及编辑器快照事务仍有同步路径，不能把异步球形操作等同于所有破坏工作已经后台化。

地形球形修改复用 CPU 粗块缓存，只重建脏块及法线梯度邻域；碰撞按 4³ 单元叶与连续 64 槽组树复用。MCP 事务、restore、Load 和 Reset 仍全块准备；渲染合并为完整 Mesh 后上传。核心只接收单网格或不可变分段列表，不包含体素策略，细节见[地形碰撞](terrain-collision.md)。

## 地基、撞击与导航

1. 调用方以实际命中点分别请求地形修改与建筑球形损伤，检查每次返回状态。
2. 地形及其碰撞成功发布后，通过 `DestructionComponent.RefreshSupport` 检查原锚点的实际底面覆盖，显式解除失效 world bonds；填土不自动重新锚定。
3. Blast 计算连通组拆分；每组使用瞬态 `Node + MeshRenderer + Collider(compound) + RigidBody`，失去锚定后由 Jolt 模拟。继续接触仍可能形成物理支撑。
4. 调用方读取物理接触快照决定二次损伤，并设置去重、冷却及次数预算。`estimatedImpulse` 为 Jolt 碰前估算，非求解后实际冲量；回调不直接执行脚本或修改场景。
5. 合并地形/静态结构的新旧变化范围，通过现有导航接口局部重建瓦片；失败失效相关旧瓦片。移动碎块不自动加入导航，不按每帧位置变化请求重建。
6. 成功的破坏结果再驱动已有特效与音频组件，表现不是地形或 Blast 求解的依赖。

`RefreshSupport` 使用指定静态 Collider 向下射线及三点面积加权底面采样，当前适用于平面底部试件；它近似地基覆盖，不是完整承重分析。`ApplyStress` 处理当前连接图、重力载荷和明确材料阈值。核心已有 DetourCrowd 路径跟随与避让，导航代理由独立插件封装；它不提供残骸自动导航或 Jolt 动态击退融合。

## 碎块、快照与恢复

`TerrainComponent` 与 `DestructionComponent` 的 `RetireFragments(maxCount, maxAgeSeconds)` 显式按数量/模拟寿命回收脱离碎块；年龄随世界模拟推进，暂停不累计。`maxAgeSeconds = 0` 关闭年龄条件，数量约束仍有效；建筑锚定组不参与回收。继续损伤不得复活已回收片块，建筑快照保存 retired chunk 身份；Reset 明确恢复初始资源。契约见[碎块回收类图](class-diagrams/fragment-retirement.md)。

初始体积、破坏描述及其网格通过正式 GUID 与依赖目录使用。`.terrain` / `.destruction` 均为 TOML 源描述；运行时只加载编译资源，不保存 Blast 内存镜像。生成节点与显示/碰撞组件标记瞬态，场景只保存源组件和配置。

地形 `TerrainStateData` 保存完整静态体积与基础 GUID/hash；加载校验基础身份，不覆盖源资源。曾提取动态土石后，静态快照无法表达完整世界，snapshot/save/restore/load 明确拒绝直到 Reset 或换源；即使土石已回收也不自动解除此限制。编辑器状态位于 `cache/terrain-states/`，脚本存档位于 App 持久目录的命名槽。

建筑快照保存基础 GUID/hash、连接血量、组姿态、速度与回收身份，服务编辑器撤销/重做；自然物理运动不使损伤 revision 过期，外部显式损伤会阻止旧记录覆盖。独立建筑游戏存档槽及跨地形/建筑/业务的综合世界存档尚未实现；单模块快照和 Reset 不等于完整游戏存档。

## 适用范围

- 地形当前为浮点密度，每轴最多 256 格、总样本最多 4M；源支持参数平面、正弦起伏及球形洞穴，没有高度图导入、逐样本材质层、FastNoise2 或 LOD transition。
- 建筑制作当前为最多 256 片的参数盒体，支持切片/Voronoi 和实际接触连接；任意 FBX、空心多层建筑、层级碎裂及运行时重新切割尚未提供。
- 动态土石和建筑根节点要求正等比缩放；动态碰撞使用凸形/compound，静态地形继续使用分段三角网格。
- 静态保存与动态碎块生命周期、离线 GI 失效、通行及导航更新分别处理，不以重置成功推断这些边界已经验收。

各原生契约见[地形类图](class-diagrams/terrain.md)、[建筑类图](class-diagrams/destruction.md)、[Blast 图](class-diagrams/destruction-graph.md)和[导航核心](../../runtime/class-diagrams/navigation-system.md)。公开范例的固定火箭数、池容量、应力/导航节奏和验证操作留在[项目说明](../../../projects/showcase/docs/destruction-integration.md)与[测试文档](../../testing.md)，不作为引擎契约重复维护。
