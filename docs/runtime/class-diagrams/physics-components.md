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
    Component <|-- Collider
    Component <|-- RigidBody
    Node *-- Component : 独占无类型组件列表
    Collider ..> ColliderDataT : 生成数据
    RigidBody ..> RigidBodyDataT : 生成数据
    Collider ..> ComponentDataT : 恢复参数
    RigidBody ..> ComponentDataT : 恢复参数
    RigidBody ..> Vec3 : 冲量与速度
    PhysicsSystem ..> RigidBody : 友元消费瞬态冲量
```

设置失败保持已有数据。冲量只接受启用的动态刚体；静态刚体拒绝非零速度。`valid_settings` 可由编辑器导入器复用。所有尺寸、质量和速度必须有限并处于校验范围；瞬态冲量不进入持久数据。
