# 物理组件

[物理系统](physics-system.md) · [节点](node.md) · [组件](component.md)

组件位于 `engine`，编入场景模块；数据来自 FlatBuffers，组件不调用 Jolt。

```mermaid
classDiagram
    class Collider {
        <<final>>
        +valid_settings(const ColliderDataT& settings) bool$
        +settings() const ColliderDataT&
        +set_settings(const ColliderDataT& settings) bool
        +restore_data(const ComponentDataT& data) bool
        +set_mesh(string_view path) bool
        +mesh_path() const string_view
    }
    class RigidBody {
        <<final>>
        +valid_settings(const RigidBodyDataT& settings) bool$
        +settings() const RigidBodyDataT&
        +set_settings(const RigidBodyDataT& settings) bool
        +restore_data(const ComponentDataT& data) bool
        +add_impulse(const Vec3& impulse) bool
        +set_linear_velocity(const Vec3& velocity) bool
        +linear_velocity() const Vec3&
    }
    class Component
    class Node
    class PhysicsSystem
    class ColliderDataT
    class RigidBodyDataT
    class ComponentDataT
    class Vec3
    class Resource
    class ResourceSystem
    Component <|-- Collider
    Component <|-- RigidBody
    Node *-- Component : 独占无类型组件列表
    Collider ..> ColliderDataT : 生成数据
    Collider o-- Resource : 共享持有独立碰撞网格
    Collider ..> ResourceSystem : 通过加载接口解析网格
    RigidBody ..> RigidBodyDataT : 生成数据
    Collider ..> RigidBody : 校验静态网格运动限制
    RigidBody ..> Collider : 拒绝网格动态及运动学状态
    Collider ..> ComponentDataT : 恢复参数
    RigidBody ..> ComponentDataT : 恢复参数
    RigidBody ..> Vec3 : 冲量与速度
    PhysicsSystem ..> RigidBody : 友元消费瞬态冲量
    PhysicsSystem ..> Collider : 友元读取已加载网格
```

设置失败保持已有数据。冲量只接受启用的动态刚体；静态刚体拒绝非零速度。`valid_settings` 可由编辑器导入器复用。所有尺寸、质量和速度必须有限并处于校验范围；瞬态冲量不进入持久数据。

`ColliderShape::mesh` 以 `ColliderDataT::mesh_guid` 引用独立 Mesh 资源，不依赖同节点的 MeshRenderer。`set_mesh(path)` 选择静态三角网格并同步加载资源，路径不存在或类型错误时保持旧设置；`mesh_path()` 返回当前持有网格的逻辑路径。切回基本形状须清空 mesh_guid 并释放引用。网格仅允许无 RigidBody 或 static_body；Collider 和 RigidBody 两端的设置操作都拒绝动态、运动学组合，物理同步再次校验。网格是资源中的静态顶点，不随蒙皮动画变形。
