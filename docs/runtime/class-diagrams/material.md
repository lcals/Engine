# 材质

[类图索引](../../class-diagrams/index.md) · [组件](component.md) · [资源系统](resource-system.md) · [共享数据模型](scene-data.md)

```mermaid
classDiagram
    class Material {
        +type_path() string_view
        +shader_path() string_view
        +shader_data() const ShaderDataT#42;
        +set_type(string_view path) bool
        +set_shader(string_view path) bool
        +set_float(string_view name, float value) bool
        +set_v2(string_view name, const Vec2& value) bool
        +set_v3(string_view name, const Vec3& value) bool
        +set_v4(string_view name, const Vec4& value) bool
        +set_texture(string_view name, string_view path) bool
        +get_texture(string_view name, string_view& output) bool
        +texture(string_view name) const ResourcePtr&
        +get_float(string_view name, float& output) bool
        +get_v2(string_view name, Vec2& output) bool
        +get_v3(string_view name, Vec3& output) bool
        +get_v4(string_view name, Vec4& output) bool
    }
    class MeshRenderer
    class ResourceSystem
    class ResourceLoader
    class Resource
    class MaterialDataT
    class ShaderDataT
    class ShaderParameterT
    class Vec2
    class Vec3
    class Vec4
    MeshRenderer "1" *-- "1..*" Material : 参数行为
    Material *-- MaterialDataT : 独占生成的实例数据
    Material ..> ResourceSystem : 路径与GUID查询
    Material ..> ResourceLoader : 取得所选实现的资源引用
    Material o-- Resource : 材质与当前Shader共享引用
    Material ..> ShaderDataT : 查询声明的参数
    Material ..> ShaderParameterT : 名称类型范围匹配
    Material ..> Vec2 : 参数和输出类型
    Material ..> Vec3 : 参数和输出类型
    Material ..> Vec4 : 参数和输出类型
```

- 材质类型与 Shader 均通过包含来源的字符串唯一路径标识：`builtin/...` 是引擎内置，`project/...` 是项目资源。同相对名字可在两个空间独立存在；没有额外来源枚举或冗余状态。资源定义可自定义组合，内置定义由引擎注册，项目资源按运行目录从散文件或合并包加载。
- `set_type()` 查找材质定义，合并其 Shader 的可修改默认参数与材质定义覆盖值。换型重建实例参数；相同材质路径保持当前实例值和 Shader 选择。未知资源返回 false，不改变状态。
- `set_shader()` 保留材质路径；切换到不同 Shader 时重新建立其可修改默认参数，旧参数和材质覆盖值不带入新组合。设置同一 Shader 路径保留已有实例参数。
- 参数名区分大小写；setter 同时验证声明的类型、`exposed`、有限数值、GPU float 可表示范围及声明的上下限。未知参数、隐藏参数、类型不匹配和非法值均返回 false，不新增未知字段、不改变旧值。
- `set_v2("pos", Vec2(0, 0))` 在选定 Shader 声明了公开的 Vec2 参数 `pos` 时成功。Vec3、Vec4 与 float 同样按声明匹配；不是仅支持内置参数的固定映射。
- `get_*()` 读取同一实例数据，失败输出零值。隐藏参数由渲染器读取 Shader 定义中的默认值，不允许通过材质接口覆盖。
- Material 由 MeshRenderer 独占，持有生成 `MaterialDataT` 的唯一运行时实例。字符串返回值和组件引用均为借用，切换材质或删除节点后不能继续使用旧借用。
- 默认 MeshRenderer 初始化选择 `builtin/materials/default-pbr`；资源定义不存在时通过组件初始化返回 false，Node 不发布半初始化组件。ResourceSystem 必须先于使用材质的场景初始化、晚于场景和 GPU 资源关闭。

- 项目材质定义可引用内置或项目 Shader；内置材质定义不能引用项目 Shader。材质持有定义与当前 Shader 的共享引用；换型失败保留旧引用。最后一个使用者释放后项目资源自动回收；`builtin/` 定义保留至资源系统关闭。

- Shader 通过具名 sampler 声明贴图槽和内置默认资源；Material 按名称覆盖贴图路径，持有 ResourcePtr。切换材质或 Shader 时先加载完整候选集合，失败保留旧值。get_texture 失败输出空视图，texture 返回借用的共享引用（未知名称为空），借用在材质修改前有效。

- `shader_data()` 为 const noexcept，只读借用当前材质持有的 Shader 资源数据；不经过可选插件缓存查询。材质换型、Shader 切换或实例销毁后旧借用失效，目录换代不使已有材质转向其他版本。
