# 节点组件

[类图索引](../../class-diagrams/index.md) · [节点](node.md) · [材质](material.md)

本页定义 Component 基类及变换、相机、灯光、网格组件；[物理组件](physics-components.md)与[音频组件](audio-components.md)独立维护，同样继承 Component 并由 Node 管理。

```mermaid
classDiagram
    class Component {
        <<abstract>>
        +~Component()
        +node() Node&
        +node() const Node&
        +type() ComponentType
        +enabled() bool
        +set_enabled(bool enabled) void
        +data() const ComponentDataT&
        +build_data(ComponentDataT& output) bool
        +restore_data(const ComponentDataT& data) bool
        #initialize() bool
    }
    class Transform {
        +settings() const TransformSettings&
        +set_settings(const TransformSettings& settings) bool
        +world_matrix(float#42; output) bool
    }
    class Camera {
        +settings() const CameraSettings&
        +set_settings(const CameraSettings& settings) bool
    }
    class Light {
        +settings() const LightSettings&
        +set_settings(const LightSettings& settings) bool
    }
    class MeshRenderer {
        +mesh_path() string_view
        +set_mesh(string_view path) bool
        +material() Material&
        +material() const Material&
        +material(size_t slot) Material#42;
        +material(size_t slot) const Material#42;
        +material_count() size_t
        +set_material_count(size_t count) bool
        +set_mesh_data(const MeshDataT& data) bool
        +mesh_data() const MeshDataT#42;
        +mesh_revision() uint64_t
        +mesh_instance_id() uint64_t
        +visible() bool
        +set_visible(bool value) void
        +cast_shadows() bool
        +set_cast_shadows(bool value) void
        +set_deformation(span~const MeshVertex~ vertices, span~const Vec4~ tangents) bool
        +clear_deformation() void
        +deformed_vertices() span~const MeshVertex~
        +deformed_tangents() span~const Vec4~
        +deformation_revision() uint64_t
        -initialize() bool
    }
    class Node
    class ComponentDataT {
        <<FlatBuffers生成>>
        +bool enabled
        +ComponentPayloadUnion payload
    }
    class ComponentPayloadUnion {
        <<FlatBuffers生成>>
    }
    class TransformDataT {
        <<FlatBuffers生成>>
        +Vec3 position
        +Vec3 rotation
        +Vec3 scale
    }
    class CameraDataT {
        <<FlatBuffers生成>>
        +float fov_degrees
        +float near_clip
        +float far_clip
        +Projection projection
        +float orthographic_size
    }
    class LightDataT {
        <<FlatBuffers生成>>
        +Color color
        +float intensity
        +LightKind kind
        +float range
        +float inner_angle
        +float outer_angle
        +bool cast_shadows
        +uint16_t shadow_resolution
        +float shadow_bias
    }
    class MeshRendererDataT {
        <<FlatBuffers生成>>
        +string mesh_guid
        +string material_guid
        +vector~string~ additional_material_guids
        +bool cast_shadows
    }
    class MeshDataT
    class MeshUtils
    class Resource
    class Material
    class MaterialDataT
    Component <|-- Transform
    Component <|-- Camera
    Component <|-- Light
    Component <|-- MeshRenderer
    Node "1" *-- "0..*" Component : 独占
    Component --> Node : 借用所属节点
    Component "1" *-- "1" ComponentDataT : 唯一组件状态
    ComponentDataT *-- ComponentPayloadUnion : 类型与数据
    ComponentPayloadUnion *-- TransformDataT : 可选载荷
    ComponentPayloadUnion *-- CameraDataT : 可选载荷
    ComponentPayloadUnion *-- LightDataT : 可选载荷
    ComponentPayloadUnion *-- MeshRendererDataT : 可选载荷
    MeshRendererDataT ..> MaterialDataT : GUID引用定义
    Transform ..> TransformDataT : settings别名
    Camera ..> CameraDataT : settings别名
    Light ..> LightDataT : settings别名
    MeshRenderer "1" *-- "1..*" Material : 按槽独占参数行为
    MeshRenderer "1" *-- "0..1" MeshDataT : 瞬时网格独立快照
    MeshRenderer ..> MeshUtils : 检查完整几何后提交
    MeshRenderer o-- Resource : 当前网格共享引用
    Material *-- MaterialDataT : 独占生成的运行时实例
```

- 所有状态类型由 FlatBuffers schema 生成。`TransformSettings`、`CameraSettings`、`LightSettings` 是生成类型的别名；Material 是按名字设置参数的行为类；Vec2/Vec3/Vec4/Color 也是生成结构，通过 `x()` / `mutate_x()`、`r()` / `mutate_r()` 访问。没有手写重复字段。
- Node 拥有者在组件进入列表之前调用受保护的 `initialize()`；默认实现成功，MeshRenderer 以私有 override 实现该钩子，选择 `builtin/materials/default-pbr`。失败销毁未发布组件并返回空指针。
- Component 独占生成的 `ComponentDataT`，包含 enabled 与带类型的 payload；派生组件直接读写自己的 payload，不另保存一份参数。具体组件提供静态 `component_type`，Node 只用通用枚举比较。
- MeshRenderer 的 `cast_shadows` 默认 true，直接存储于生成的组件载荷并随场景保存；false 时仍参与主绘制，但不参与阴影边界拟合和阴影绘制，供天空等表面使用。`visible` 仍为不落盘的瞬态开关，两者不互相替代。
- Transform 是普通组件。Scene 创建 Node 后添加 Transform，变换逻辑全部位于 Transform。位置和欧拉角用 double，角度单位为度，矩阵为 `T * Rz * Ry * Rx * S`，Y 向上、局部前向 -Z。
- 变换必须全部有限，缩放非零，可为负数；失败保持原值。`world_matrix()` 写 16 个列主序 float，父链缺失变换或溢出时返回 false，非空输出保持单位矩阵。
- Camera 从同节点 Transform 获得位置和朝向；视场角在 (0,180)，近裁面大于 0，远裁面大于近裁面。Camera 支持透视与正交投影；orthographic_size 为竖直半高且大于零。Light 支持方向光、点光与聚光；方向使用世界 -Z，强度非负且有限，range 大于零，聚光半角满足 0 <= inner_angle < outer_angle < 90。
- MeshRenderer 默认使用内置 cube，通过 set_mesh 按 GUID 加载并持有 Mesh 资源；成功取得新引用后才替换旧网格，提供 Material 借用。材质先选择类型，再按名称设置带类型参数，具体参数及失败规则见独立材质类图。
- 禁用不改变所有权；Transform 的启用标记不改变层级空间关系。GPU 缓冲、Shader 和管线仍由 RenderSystem 管理。

- Light 的投影开关默认关闭，当前投影仅支持方向光；阴影分辨率默认 2048，范围 128..8192 且为 2 的幂，深度偏移默认 0.001、范围 0..0.1。[环境与后处理](environment-components.md)使用独立组件承载场景环境与相机画面参数。

- `data()` 提供生成组件数据的只读借用，调用者不得修改或转移其嵌套载荷；常规变更通过具体组件的方法完成。MeshRenderer 的材质引用由 MeshRenderer::build_data 根据当前材质路径解析 GUID；场景不嵌入整个运行时材质实例。

- `material()` 保留槽 0 的必需引用；按槽查询越界返回空指针，材质槽数量为 1..16。缩减不得丢弃当前子网格引用的槽。扩容先初始化默认材质，全部成功才提交。
- `set_mesh_data()` 接收 FlatBuffers 生成的独立几何快照，校验顶点、索引、UV、切线、颜色与子网格范围后整体替换，并按需扩充材质槽；失败不改变现有几何、槽与修订号。动态数据不注册到资源单例，不伪造 GUID。
- `set_mesh()` 取得完整资源后替换瞬时快照；两种成功提交均递增 mesh_revision。`mesh_data()` 是当前组件持有数据的只读借用，下次成功变更或组件销毁后失效。
- `mesh_instance_id()` 是进程内稳定且不重复的组件实例身份；瞬态蒙皮/GPU 缓存以它和 `mesh_revision()` 区分同节点组件重建，不依赖可能重复的内存地址。
- `set_deformation()` 接收与源顶点等长的位置/法线及切线，完整校验后独立保存，仅递增 deformation_revision；失败保留旧输出。源 Mesh、GUID、索引、UV、颜色、子网格与材质均不变。两个 span 查询返回借用，下一次成功提交、清除、源网格变化或组件销毁后失效。
- `clear_deformation()` 恢复源几何，重复调用无副作用；成功更换源网格或恢复组件数据也清除旧变形。变形数据不进入场景快照。清除及只读查询为 noexcept，渲染器同时检查修订号与变形是否存在；修订号到达上限时拒绝新提交，清除仍可执行。

- `build_data` 和 `restore_data` 是统一组件持久数据契约；简单组件直接校验生成载荷，MeshRenderer 在自身文件解析 GUID、持有资源和组装材质槽。Scene 只组织节点与层级，不分派具体组件字段。失败快照为空，恢复失败保留组件原状态。

`MeshRenderer::visible()` / `set_visible()` 控制瞬态绘制可见性，默认 true；不改组件 enabled、源资源、动画采样或持久快照。隐藏时主通道与阴影通道均不绘制；再次显示按已有几何和当前变形准备 GPU 资源。
