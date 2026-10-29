# 节点

[类图索引](../../class-diagrams/index.md) · [场景](scene.md) · [组件](component.md)

```mermaid
classDiagram
    class Node {
        +~Node()
        +id() uint64_t
        +name() string_view
        +scene() Scene&
        +scene() const Scene&
        +transform() const Transform&
        +set_transform(const Transform& value) bool
        +parent() Node#42;
        +parent() const Node#42;
        +set_parent(Node#42; parent) bool
        +world_matrix(float* output) bool
        +add_camera() Camera#42;
        +add_light() Light#42;
        +add_mesh_renderer() MeshRenderer#42;
        +camera() Camera#42;
        +camera() const Camera#42;
        +light() Light#42;
        +light() const Light#42;
        +mesh_renderer() MeshRenderer#42;
        +mesh_renderer() const MeshRenderer#42;
    }
    class Scene
    class Component
    class Camera
    class Light
    class MeshRenderer
    class Transform {
        <<struct>>
        +Vec3 position
        +Vec3 rotation
        +Vec3 scale
    }
    class Vec3 {
        <<struct>>
        +double x
        +double y
        +double z
    }
    Scene "1" *-- "0..*" Node : 拥有
    Node --> Scene : 借用所属场景
    Node --> Node : 可空父节点借用
    Node "1" *-- "0..*" Component : 独占组件
    Node "1" *-- "1" Transform : 唯一变换数据
    Transform *-- Vec3 : 位置欧拉角缩放
    Node ..> Camera : 添加与查询返回
    Node ..> Light : 添加与查询返回
    Node ..> MeshRenderer : 添加与查询返回
```

- Node 是所有场景对象的身份、层级与组件容器；Transform 是每个节点固有的普通数据结构，不额外制造变换实体或第二份 ECS 状态。
- 位置和欧拉角使用 double；欧拉角单位为度，矩阵为 `T * Rz * Ry * Rx * S`。Y 向上，相机和灯光局部前向为 -Z。
- 设置变换必须全部有限，缩放各分量非零；失败保持原值。允许负缩放。
- `set_parent()` 保持局部变换；空指针解除父子关系。拒绝其他场景节点、自身及循环父链。
- `world_matrix()` 写 16 个列主序 float；父链变换累乘出现溢出返回 false，输出保持单位矩阵；空输出返回 false。
- 每个节点当前每类内置组件至多一个，重复添加返回空指针。组件独占于节点、不可单独转移。本阶段不增加通用类型注册或模板分派。
- 节点析构删除底层 flecs 实体；删除层级统一走 Scene，调用方不得销毁借用。
