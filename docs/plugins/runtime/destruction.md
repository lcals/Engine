# 建筑破坏插件

`runtime/destruction` 使用固定版本 NVIDIA Blast 5.1 的 low-level 支撑图和
`NvBlastExtFalloffGraphShader`，保留部分连接损伤，由官方拆分结果决定连通碎块组。
`editor/destruction` 使用 Blast Authoring 切片/Voronoi、官方三角体积积分与 EXACT 接触连接生成器；
没有新增自研 Voronoi、连通性或应力算法。预破碎的 Boost Multiprecision 仅为编辑器依赖，
运行包不包含 Authoring、Boost 或 PhysX。

自制试件由 `.destruction` TOML 资源描述：`size`、`fracture`、`slices_x/y/z`（最终块数）、
`sites`、`seed`、`bond_health` 与 `anchor_height`，源文件和邻接唯一 GUID `.meta` 进入现有增量管线。
当前输入为参数盒体，最多 256 个碎块；不是任意复杂 FBX 建筑预破碎工具。
生成资源内的片段几何、体积、质心与连接全部是该可破坏体所需数据，不保留 Blast 平台内存镜像。

普通根节点挂 `DestructionComponent`，绑定资源和材料；每个连通组生成一个瞬态节点，
使用 `MeshRenderer + Collider(compound) + RigidBody`。核心使用 Jolt 官方凸包与复合形状，
支持多碎块保持一个刚体，断开锚点后参与动态模拟；剩余接触仍可能支撑碎块，
实际位移由重力、碰撞和项目配置的冲量决定。初版要求根节点和正等比缩放。
场景保存排除生成节点，组件移除或项目关闭时回收。

脚本 `DamageSphere(x,y,z,radius,damage,impulse=0)` 接收世界坐标，移动中的组仍按当前姿态受损；
`Reset()` 恢复初始图。`RetireFragments(maxCount, maxAgeSeconds)` 按数量与模拟寿命显式回收脱离动态组；锚定组保留，继续损伤/应力及快照恢复不会复活已回收片块，Reset 才清除回收身份，见[回收契约](class-diagrams/fragment-retirement.md)。`ready/revision/chunkCount/groupCount/dynamicGroups/lastError` 报告状态。
MCP 提供 `create_destruction_asset`、`set_destruction`、`get_destruction`、
`destruction_damage`、`destruction_reset`。损伤与重置使用完整快照支持撤销重做，
包含基础资源 GUID/hash、连接血量、组姿态及线/角速度；自然物理运动不使编辑记录过期，
外部损伤修改则拒绝覆盖。该快照不是独立游戏存档槽 API。

同步损伤接口保留，另提供有界异步损伤准备与版本校验后的主线程发布；两条路径都按组增量发布并复用兼容未变组的节点、网格和碰撞，仅为需要替换的组准备候选。仅速度变化通过 Jolt 原地更新，不替换 BodyID；节点查找和子树释放使用核心的 ID 索引与线性删除流程。地形地基、撞击再破坏、烟尘音效和官方 ExtStress 同步重力应力均已联通，详见[当前实现](../../runtime/performance.md#当前实现)。

## 地形与物理联动

- 核心物理提供指定 Collider 射线、通用球形扫掠和只读接触快照；`estimatedImpulse` 是 Jolt 官方碰前估算，非求解后实际冲量。核心导航复用 Recast `Sample_TileMesh` / Detour，通过明确输入的几何及范围同步构建或替换瓦片，不扫描场景识别玩法。
- 破坏插件保留地基多点覆盖检查、world bond 解除和碎块归属；`ApplyStress` 使用 Blast 原有标量求解器。兼容补丁在构建目录应用，所有平台使用同一标量路径，不加入 PhysX。它处理当前支撑图的重力应力，不等于完整建筑力学。
- 地形插件 `CollapseDetached` 识别脱离下/侧边界的连通土石，按项目指定大小生成瞬态网格和 Jolt 凸形 compound 动态节点；预算或准备失败不删除未转换土石。项目继续挖填不会吃掉已有碎块，Reset 清理生成物并恢复源体积。

场景中的命中、伤害顺序、导航脏区提交与表现由项目脚本组织。公开实验场、验收证据及平台限制见[测试与范例](../../testing.md)；制作工具见[地形与破坏制作](../editor/terrain-destruction-authoring.md)。
