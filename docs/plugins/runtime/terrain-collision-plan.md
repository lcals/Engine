# 地形碰撞局部更新方案

更新日期：2026-11-02。分段静态碰撞、4³ 单元叶子和上层缓存已实现；球形笔刷可在共享任务域准备候选，Jolt 形状发布仍在调用线程安全点完成。本文维护实现与正确性边界，性能数值统一见[最终可信基线](../../runtime/performance.md#历史单线程基线)。

## 目标与当前结果

目标是当前炮击场景的单次完整地形碰撞更新低于 **0.5 ms**。预算包含碰撞几何准备、变化识别、Jolt 叶子构建、上层组合、提交、邻近刚体唤醒及旧体积释放；同帧多次提交另报累计。首次建图、Reset 和全量恢复单独统计，不由增量炮击结果推断。

最终 macOS 实景样本中的单次增量碰撞更新满足预算；**Jolt 单步及整体物理仍未达到 0.5 ms**。该结果只适用于已记录的设备、场景、资源版本和操作窗口，不是任意地图或连射负载的硬上界，也不构成 Android、iOS、Windows 或 Linux 验收。

渲染继续合并为完整 Mesh 并完整上传 GPU；完整 `Terrain.commit` 还包含网格和渲染准备，不能整体称为碰撞更新时间。

## 已实现的结构

保留一个普通地形 Node、一个 Collider、一个静态 BodyID。Jolt 子形状不成为场景对象，射线和撞击仍返回原 Collider/节点身份；动态土块继续使用普通 Node + Collider + RigidBody。

| 层级 | 结构与更新方式 |
| --- | --- |
| 全场物体 | Jolt `BroadPhaseQuadTree`；通过官方形状提交更新世界包围盒 |
| 上层分组 | 连续 64 槽组成官方 `StaticCompoundShape` 缓存组；复用未变组，重建变化组及根 |
| 碰撞叶子 | 插件内部 4³ 单元，对应不可变 `MeshShape`；仅变化叶重新构建 BVH |
| 体素修改定位 | 规则网格整数索引定位受影响单元，保留梯度所需邻域标脏 |

持久地形块仍为 16³ 或 32³ 单元，每块对应固定 64 或 512 个碰撞槽。原 regular-cell 提取同时记录单元输出范围，之后将相同位置和索引分配到细叶；不额外执行网格提取，不减少三角形或采样精度。空叶保留稳定槽位且不创建 Shape，未变叶保留不可变网格身份。塌方仅标脏抽走样本及梯度邻域，法线与全量重建保持一致。

Jolt QuadTree 是三维 AABB 的四路树。当前实现复用上游树，未引入手写空间树、KD-tree 或新的场景层级。`StaticCompoundShape` 本身不可原地修改，局部更新仍需重建变化组和根，不能称为整条树 O(1) 更新。

## 核心接口与原子发布

[MeshCollisionData](../../runtime/class-diagrams/mesh-collision-data.md) 只持有通用 `schema::MeshDataT` 的不可变共享列表。索引为稳定槽位，nullptr 表示空槽，提交的是完整列表。调用方可释放或修改自己的列表；已发布网格不得通过可变别名修改，几何更新必须替换快照。核心不识别体素、地形资源或笔刷。

`PhysicsSystem::set_mesh_parts` 按以下流程同步执行：

1. 验证静态用途、组件配置与世界变换，复制列表并共享不可变网格。
2. 按网格身份和实际烘入缩放复用叶子；变化叶使用 Jolt 5.6.0 `MeshShapeSettings::FavorBuildSpeed`，保留 active-edge 检测。该选项只改变 BVH 构建策略，不简化几何。
3. 复用未变组并构建必要的候选组/根；只有一个有效子形状时直接使用它。
4. 全部成功后通过官方 `SetShape` 发布。局部变更以变化叶的旧、新世界 AABB 唤醒附近动态体；配置或姿态变化采用完整范围。
5. 全空列表移除碰撞。失败保留旧组件与物理形状；地形事务也保留旧密度和 revision，不发布半完成候选。

无几何变化时复用根形状，但 Collider center、摩擦、材质属性、启用状态与变换变化仍须生效。缩放以实际烘入 Shape 的值为复用基准，不能逐帧推进容差基准后累积误差。单网格 `mesh_data()` 在分段模式返回 nullptr，不能虚构合并几何；瞬时分段数据不作为空 GUID 的持久资源保存。

## 正确性与生命周期

- **接缝：** 刚体与 CharacterVirtual 使用官方 Enhanced Internal Edge Removal，继续保留 active-edge 检测。同一地形 Body 有利于跨子形状处理，不能将其拆成独立静态 Body 后假定接缝语义相同。
- **精度与变换：** 静态分段网格保留正非均匀缩放、Collider center 和父变换契约；缩放变化使相关缓存失效。动态土块仍遵守 compound 的正等比缩放限制。
- **唤醒：** 挖掉旧支撑也必须唤醒物体，不能只检查新表面范围。真实 COM、中心偏移和父变换参与世界范围计算。
- **恢复：** 禁用/重启用、清空/恢复、组件替换、切场景和物理世界清理后，现存网格来源足以重建碰撞，不能只保存 Jolt Shape。
- **线程：** 私有密度/网格候选可后台准备；`set_mesh_parts` 的 Jolt 形状构建与发布仍在同步安全点执行，不与 Jolt 步进、查询或场景修改并发，不原地修改活动 Shape。
- **存档：** Jolt Shape 不进入资源或存档。发生土石塌方后，现有静态体积存档不能完整记录动态土块，保存/恢复继续明确拒绝，直到 Reset 或更换源资源。

相关定向回归已覆盖分段失败原子性、空叶、缩放/父变换、跨缝、旧支撑唤醒、组件生命周期及塌方法线。实际验证配置和平台范围以[性能文档](../../runtime/performance.md#历史单线程基线)及[跨平台记录](terrain-destruction-feasibility.md)为准。

## 计时与剩余工作

`Terrain.prepare_collision` 包含叶子分配、已生成几何复制、比较和快照替换；`Physics.set_mesh_parts` 包含核心碰撞准备与提交；`Terrain.release_previous_volume` 包含旧几何最后引用释放，并保守包含旧密度释放。完整预算累计三者，`Terrain.collision_compare` 已属于准备时间，不重复相加。范围记录仍在 `Terrain.mesh_chunk` 内，保留完整 `Terrain.commit` 以检查总成本。

分段复用已落地；后续仍需处理首次建图/Reset 的全量准备成本、同帧重复提交、地形连通扫描与塌方事务；渲染分块上传尚未实现。Jolt 窄相、接触和追赶帧仍是物理预算缺口，具体数据及后续方向统一见[当前实现](../../runtime/performance.md#当前实现)和[最终可信基线](../../runtime/performance.md#历史单线程基线)。

`MutableCompoundShape` 可作为后续上层更新的备选，但只能在测量证明有收益后比较。无论使用 mutable 或 static，`SetShape` / `NotifyShapeChanged` 都会使该 Body 的接触缓存失效，叶子复用不能绕过官方失效契约。异步球形笔刷已使用独占快照、版本校验、有界请求与主线程发布；过期结果不覆盖活动状态。Reset、首次构建及连通塌方保留同步边界，不能把整个场景事务直接移到后台。执行契约见[地形类图](class-diagrams/terrain.md)，采样必须区分 worker 准备和主线程 `Terrain.publish`。

## 开源来源

当前实现复用 Jolt **5.6.0**（MIT）的公开接口和算法，未修改上游依赖。下列链接保留最初方案采用的 5.4.0 源码位置；依赖升级不改写设计来源。地形表面提取复用固定版本 Transvoxel（MIT），版本与来源见[总体方案](terrain-destruction-plan.md)。商业/私有参考源码、实现和资源不得进入仓库。

- [MeshShapeSettings 与构建质量](https://github.com/jrouwe/JoltPhysics/blob/v5.4.0/Jolt/Physics/Collision/Shape/MeshShape.h)
- [MeshShape BVH 实现](https://github.com/jrouwe/JoltPhysics/blob/v5.4.0/Jolt/Physics/Collision/Shape/MeshShape.cpp)
- [StaticCompoundShape](https://github.com/jrouwe/JoltPhysics/blob/v5.4.0/Jolt/Physics/Collision/Shape/StaticCompoundShape.h)
- [MutableCompoundShape](https://github.com/jrouwe/JoltPhysics/blob/v5.4.0/Jolt/Physics/Collision/Shape/MutableCompoundShape.h)
- [官方多 MeshShape 组合范例](https://github.com/jrouwe/JoltPhysics/blob/v5.4.0/Samples/Tests/Shapes/MeshShapeUserDataTest.cpp)
- [SetShape / NotifyShapeChanged](https://github.com/jrouwe/JoltPhysics/blob/v5.4.0/Jolt/Physics/Body/BodyInterface.cpp)
- [Ghost Collisions 与内部边消除](https://github.com/jrouwe/JoltPhysics/blob/v5.4.0/Docs/Architecture.md#ghost-collisions)
- [BroadPhase QuadTree](https://github.com/jrouwe/JoltPhysics/blob/v5.4.0/Jolt/Physics/Collision/BroadPhase/QuadTree.h)

实现入口：[地形分块](../../../src/engine/plugins/runtime/terrain/terrain_volume.cpp)、[地形提交](../../../src/engine/plugins/runtime/terrain/terrain_system.cpp)、[核心物理](../../../src/engine/runtime/physics/physics_system.cpp)。
