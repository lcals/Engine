# 组件类型登记

[类图索引](../../class-diagrams/index.md) · [组件](component.md) · [ECS 场景](ecs-scene.md)

```mermaid
classDiagram
    class ComponentRegistry {
        +add~T~() bool
        +contains(ComponentType type) bool
        +create(ComponentType type, Node node) Component
        +find(ComponentType type, Node node) Component
        +remove(ComponentType type, Node node) bool
        +collect(Node node, vector~Component~& output) bool
        +operations(ComponentType type) const Operations#42;
        +register_type(ComponentType type, const Operations& operations) bool
        +native_world() ecs_world_t#42;
    }
    class Operations {
        <<per-type function table>>
        +create(Node) Component
        +remove(Node) bool
        +state(Node) ComponentState#42;
        +data(Node) void#42;
        +runtime(Node) void#42;
        +snapshot(Component, ComponentDataT&) bool
        +build(Component, ComponentDataT&) bool
        +restore(Component, const ComponentDataT&) bool
        +remap(Component, ComponentDataT&, span~const NodeIdMappingT~) bool
    }
    class ComponentState {
        <<runtime metadata>>
        +ComponentFlagsT flags
        +uint64_t instance_id
        +uint64_t revision
        +bool transient
    }
    class WorldSystem
    class ecs_world_t
    class Component
    class Node
    class ComponentType
    class ComponentDataT
    class NodeIdMappingT
    class ComponentFlagsT
    WorldSystem *-- ComponentRegistry : 独占登记
    ComponentRegistry *-- Operations : 每类型一份操作表
    ComponentRegistry --> ecs_world_t : 借用世界
    ComponentRegistry ..> Node : 目标值句柄
    ComponentRegistry ..> Component : 返回值句柄
    ComponentRegistry ..> ComponentType : 生成枚举
    Operations ..> ComponentState : 访问当前列元数据
    Operations ..> ComponentDataT : 边界快照
    Operations ..> NodeIdMappingT : 映射资源局部身份
    ComponentState *-- ComponentFlagsT : 唯一生成标志数据
```

`add<T>()` 调用类型自身的登记方法，注册具体生成 Data 的 Flecs 列、可选 Runtime 列及上述静态操作。每类型一份函数表，不为每个实例创建行为对象或虚函数表。表中的条目为函数指针，图中以其签名表示。

重复类型、NONE、无效操作登记失败；未知类型创建返回空 Component。创建先建立列和新实例身份，再执行具名类型初始化；任一步失败删除本次列。删除再添加获得新身份，普通恢复和 ECS 移表不改变实例身份。

ComponentStorage<Data> 含 ComponentState 与 schema 生成 Data；动态 ComponentDataT union 只用于边界快照。Runtime 保留领域缓存和请求，不重复持久参数。具体登记、校验、保存与恢复函数归所属模块；核心不依赖插件，宿主在加载前登记插件，函数地址须活到 WorldSystem 关闭。
