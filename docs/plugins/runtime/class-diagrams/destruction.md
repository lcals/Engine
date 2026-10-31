# 建筑破坏：Blast 计算与 Jolt 提交

步骤 2 使用官方 Blast Authoring 预破碎、ExtDamageShaders 径向损伤及 low-level 拆分。
不另写 Voronoi、连接连通性或应力求解器。运行插件只借用核心世界和物理；
Authoring 与 MCP 属于编辑器插件。核心增加的凸包复合碰撞、瞬态节点均不识别 Blast。

```mermaid
classDiagram
class System
class Component
class WorldSystem
class PhysicsSystem
class Resource
class DestructionGraph
class DestructionDataT
class DestructionStateDataT
class DestructionComponent {
+settings() DestructionDataT
+set_settings(DestructionDataT value) bool
+restore_data(ComponentDataT data) bool
+damage_sphere(Vec3 center, float radius, float damage, float impulse) bool
+reset() bool
+snapshot(ByteBuffer output) bool
+restore(ByteView bytes) bool
+ready() bool
+revision() uint64_t
+chunk_count() size_t
+group_count() size_t
+dynamic_groups() size_t
+last_error() string
}
class DestructionSystem {
+DestructionSystem(WorldSystem world, PhysicsSystem physics)
+instance() DestructionSystem*
+synchronize() bool
+synchronize(Node node) bool
+rebuild(DestructionComponent component, DestructionGraph candidate, bool preserve) bool
+damage(DestructionComponent component, Vec3 center, float radius, float damage, float impulse) bool
+reset(DestructionComponent component) bool
+snapshot(DestructionComponent component, DestructionStateDataT output) bool
+restore(DestructionComponent component, DestructionStateDataT state) bool
#on_initialize(EngineConfig config) bool
#on_tick(double delta_time) bool
#on_shutdown() void
}
System <|-- DestructionSystem
Component <|-- DestructionComponent
DestructionComponent *-- DestructionGraph : independent damage state
DestructionComponent --> Resource : shared immutable source
DestructionComponent ..> DestructionDataT : generated settings
DestructionComponent ..> DestructionStateDataT : portable snapshot
DestructionSystem --> WorldSystem : borrowed
DestructionSystem --> PhysicsSystem : borrowed
DestructionSystem ..> DestructionComponent : prepares and publishes
DestructionSystem ..> DestructionGraph : candidate clone
```

建筑是普通根节点挂 DestructionComponent，首版要求正等比缩放。每个 Blast 连通组对应
一个生成的瞬态根节点，拥有 MeshRenderer、凸包复合 Collider 和 RigidBody。
仍与 world bond 相连的组固定；脱离支撑的组使用 Jolt 动态刚体，继续允许受损拆分。
插件保留组的节点 ID，按场景及组件生命周期回收，不把生成节点写进场景。
破坏组件与源 Transform 均启用时，生成组才显示并参与物理；禁用时同步移除碰撞，
保留姿态与速度，状态恢复也遵守同一启用条件。

损伤使用世界坐标，按每组当前姿态转换为资产局部坐标。候选 graph、网格与碰撞先
准备，全部成功再替换旧组；断裂后继承原组姿态及速度。部分损伤保留而不强制拆块。
快照使用 FlatBuffers 的基础 GUID/hash、稳定 bond 血量以及各组块 ID、姿态和速度，
不保存 Blast ABI 内存。状态恢复先校验再提交；正常场景保存只保存初始资源与组件配置。

系统在物理前由父系统驱动；编辑器预览调用 synchronize，不直接 tick 子系统。
本阶段不包含地形支撑撤销、撞击反馈、承重应力、导航或异步任务；它们保留既有后续顺序。

编辑器单对象事务调用 `synchronize(Node&)`，只发布或回收该节点的输出；
每帧全局同步仍负责扫描和调度，不在编辑某一个对象时顺带发布其他待处理对象。
