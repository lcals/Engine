# 节点值句柄

[类图索引](../../class-diagrams/index.md) · [场景](scene.md) · [组件](component.md) · [ECS 场景](ecs-scene.md)

```mermaid
classDiagram
    class Node {
        <<value handle>>
        +Node()
        +valid() bool
        +id() uint64_t
        +name() string
        +local_id() uint64_t
        +build_info(NodeInfoT& output) bool
        +world() WorldSystem#42;
        +scene() Scene#42;
        +parent() Node
        +find_child(string_view relative_path) Node
        +set_parent(Node parent) bool
        +enabled() bool
        +set_enabled(bool value) bool
        +active() bool
        +visible() bool
        +set_visible(bool value) bool
        +visible_in_hierarchy() bool
        +transient() bool
        +set_transient(bool value) bool
        +persistent() bool
        +set_persistent(bool value) bool
        +add_component~T~() T
        +get_component~T~() T
        +add_component(ComponentType type) Component
        +get_component(ComponentType type) Component
        +remove_component(ComponentType type) bool
        +collect_components(vector~Component~& output) bool
        -world_ WorldSystem#42;
        -world_epoch_ uint64_t
        -entity_id_ uint64_t
    }
    class WorldSystem
    class Scene
    class Component
    class ComponentType
    class NodeInfoT {
        <<FlatBuffers snapshot>>
        +unique_ptr~NodePropertiesT~ properties
        +uint64_t parent_id
    }
    class NodePropertiesT {
        <<FlatBuffers ECS data>>
        +uint64_t id
        +string name
        +bool enabled
        +bool visible
    }
    Node --> WorldSystem : 借用且不超过世界寿命
    Node ..> Scene : 查询可空借用
    Node ..> Node : 父节点值句柄
    Node ..> Component : 创建查询枚举值句柄
    Node ..> ComponentType : 显式类型登记
    Node ..> NodeInfoT : 独立快照
    NodeInfoT *-- NodePropertiesT : 快照独立副本
```

- Node 只保存世界借用、世界生命周期代次和完整 Flecs 实体 ID。复制或析构句柄不创建或销毁实体；删除必须显式调用 `Scene::destroy_node()`。
- `valid()` 同时检查世界代次与实体代次。WorldSystem 对象须活过句柄的使用；shutdown/reinitialize 后旧句柄失效，不承诺世界对象析构后仍能解引用裸借用。
- 真实节点属性为 ECS 列中的生成 `NodePropertiesT`；父子关系只存 Flecs `ChildOf`。`build_info(output)` 临时组装独立 `NodeInfoT`，返回 bool，失败清空输出，其中 `properties.id` 与 `parent_id` 为落盘局部身份，不能与 `id()` 的运行实体 ID 混用。`local_id() const noexcept` 直接读取落盘局部 ID，无效时返回 0，不组装快照；`name()` 返回字符串副本。
- `add_component<T>()` / `get_component<T>()` 返回具名组件值句柄，动态类型版本返回 Component 值句柄。每类至多一份；重复添加、缺失或初始化失败返回空句柄。Node 没有组件对象列表、具体类型指针或虚函数分派。
- 类型登记表负责 ECS 列、运行字段和初始化/保存/恢复操作，生成枚举作为唯一类型标识。`collect_components()` 仅为编辑、保存与诊断生成身份快照；原生热路径直接做有类型的 ECS 查询。
- `set_parent()` 保持局部变换。默认 `Node{}` 解除关系；过期非空、跨世界、跨场景、自身或循环父链均失败并保留旧关系。直接标记常驻的根拒绝非空父节点。
- `remove_component()` 移除真实 ECS 数据，旧组件实例身份立即失效；重新添加不会复活旧句柄。节点自身身份不变。

## 相对子路径查询

`find_child(string_view relative_path) const noexcept` 为 `[[nodiscard]]`，返回可空 Node 值句柄。`Node.FindChild(path)` 将树查询委托给 Node 领域，脚本绑定继续负责参数与 Run 身份，不增加查询服务、节点对象容器或名称索引。

- 起点必须是有效 Node。相对路径以 `/` 分段，每段只匹配当前节点的直接子节点；字符串大小写敏感，名称不进行文件系统规范化。
- 空路径、首尾 `/`、连续 `/`、单独 `.` 或 `..` 路径段均返回空；不返回起点、不向上跳转，也不支持场景绝对路径。反斜线按普通名称字符处理，保持现有脚本行为。
- 某段无匹配或存在两个及以上同名直接子节点时返回空，不按 ECS 表行顺序挑选。多个不同父节点下的同名子节点不构成歧义；成功时返回最后一段对应的节点。
- 不过滤隐藏、停用或瞬态节点；不跨 Scene，不查找兄弟或任意深度的同名后代。Published 与 Scene/Run 可见性边界遵守[场景查询契约](scene.md#场景查询收敛契约)。
- 每次调用读取当前 ChildOf 和真实名称；重挂父级、删除、重新添加后不使用旧树缓存。返回值仍是世界代次/完整实体身份句柄，不包含原生列借用。

节点名称本身允许包含 `/`，该方法不增加转义语法；此类名称可通过 Scene 的精确名称查询或已知运行实体 ID 访问。多级路径上的同名歧义在最先出现的歧义段失败，即使其中一条分支后续能匹配也不自动尝试其他分支，与既有 TS 行为一致。

## 子树状态与常驻

本地 `enabled` 与 `visible` 默认 true，写入生成的 NodePropertiesT 并随场景保存。有效激活和可见状态为派生缓存，由 `WorldSystem::refresh_scene_state()` 在安全点更新；修改父子/状态后查询不能使用旧帧缓存。隐藏继续模拟，停用暂停系统消费；父级恢复保留后代本地设置，Transform 仍用于层级计算。

`set_transient()` 设置运行标志，后代继承有效瞬态状态，保存跳过相应子树。`set_persistent()` 只接受根节点，重复设置幂等；后代继承常驻性。普通关卡卸载保留常驻实体、组件实例与原场景身份，显式删除、停止项目及世界关闭仍释放。瞬态与常驻互相独立，均不进入 schema；完整清理规则见[常驻节点](persistent-nodes.md)。

所有修改检查有效性与结构修改阶段并返回 bool；无效句柄状态查询为 false。结构修改及原生列借用的规则见 [ECS 场景](ecs-scene.md)。
