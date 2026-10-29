# 组件创建登记

[类图索引](../../class-diagrams/index.md) · [组件](component.md)

```mermaid
classDiagram
    class ComponentRegistry {
        +add~T~() bool
        +create(ComponentType type, Node& node) Component#42;
    }
    class Component
    class Node
    class Scene
    class ComponentType
    ComponentRegistry ..> Component : 登记工厂并返回借用
    ComponentRegistry ..> Node : 通过add_component创建
    ComponentRegistry ..> ComponentType : 生成类型标识
    Scene ..> ComponentRegistry : 按数据类型创建
    Node *-- Component : 独占
```

登记只保存创建函数，不拥有组件。重复类型与 NONE 登记失败；未知类型返回空。默认登记在一个文件中显式列出，具体组件仍由 Node 的泛型方法创建。Scene 可复用创建节点时已有的 Transform，其他类型按表创建；恢复数据交给组件自己的虚方法，失败场景不会发布给 WorldSystem。
