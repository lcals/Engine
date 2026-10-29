# FlatBuffers 共享数据模型

[类图索引](../../class-diagrams/index.md) · [资源系统](resource-system.md)

Schema 位于 `include/schemas/runtime/`，构建时生成 C++ 与 TypeScript。以下字段与 C++ 生成头一致，省略 `std::` 和 `engine::schema::` 命名空间；`unique_ptr`、容器元素类型和明确宽度整数保留原样。各资源根具有版本与文件标识，运行时验证后才解包。

## 场景与组件

```mermaid
classDiagram
    direction TB
    class SceneDataT {
        +uint32_t schema_version
        +unique_ptr~SceneInfoT~ info
        +vector~unique_ptr~NodeDataT~~ nodes
    }
    class NodeDataT {
        +unique_ptr~NodeInfoT~ info
        +vector~unique_ptr~ComponentDataT~~ components
    }
    class ComponentDataT {
        +bool enabled
        +ComponentPayloadUnion payload
    }
    class SceneInfoT
    class NodeInfoT
    class ComponentPayloadUnion
    class MeshRendererDataT {
        +string mesh_guid
        +string material_guid
    }
    SceneDataT *-- SceneInfoT : 独占场景信息
    SceneDataT *-- NodeDataT : 独占节点集合
    NodeDataT *-- NodeInfoT : 独占节点信息
    NodeDataT *-- ComponentDataT : 独占组件集合
    ComponentDataT *-- ComponentPayloadUnion : 类型与载荷
    ComponentPayloadUnion *-- MeshRendererDataT : 可选载荷之一
```

`SceneInfoT`、`NodeInfoT` 与各组件载荷分别见[场景](scene.md)、[节点](node.md)和[组件](component.md)。`scene.fbs` 只含场景、节点、组件及资源引用；数值类型来自 `common.fbs`。`MeshRendererDataT` 的两个 GUID 分别引用下方网格与材质定义，不复制资源内容。

场景节点 ID 为稳定局部 ID。加载时映射到运行实体 ID，恢复父子关系和活动相机；保存时反向映射，不将 flecs ID 落盘。TypeScript 节点 ID 使用 bigint。

## 网格

```mermaid
classDiagram
    class MeshDataT {
        +uint32_t schema_version
        +string path
        +vector~MeshVertex~ vertices
        +vector~uint32_t~ indices
    }
    class MeshVertex
    class MeshRendererDataT
    MeshDataT *-- MeshVertex : 值类型顶点集合
    MeshRendererDataT ..> MeshDataT : mesh_guid引用
```

`MeshVertex` 是生成的值结构，包含位置和法线；顶点容器不使用 `unique_ptr`。资源路径与引用 GUID 的对应关系由资源目录维护。

## 材质

```mermaid
classDiagram
    class MaterialDataT {
        +uint32_t schema_version
        +string shader_guid
        +string type_path
        +string shader_path
        +vector~unique_ptr~MaterialParameterT~~ parameters
    }
    class MaterialParameterT {
        +string name
        +MaterialValueUnion value
    }
    class MaterialValueUnion
    class MeshRendererDataT
    class ShaderDataT
    MaterialDataT *-- MaterialParameterT : 独占参数集合
    MaterialParameterT *-- MaterialValueUnion : 带类型数值
    MeshRendererDataT ..> MaterialDataT : material_guid引用
    MaterialDataT ..> ShaderDataT : shader_guid引用
```

场景引用独立材质资源，不嵌入 Shader 字节码或整个材质定义。MeshRenderer 独占 Material 行为对象，Material 自己持有生成的 `MaterialDataT` 运行实例，支持命名参数修改。

## Shader

```mermaid
classDiagram
    class ShaderDataT {
        +uint32_t schema_version
        +string path
        +vector~uint8_t~ vertex_code
        +vector~uint8_t~ fragment_code
        +vector~unique_ptr~ShaderParameterT~~ parameters
    }
    class ShaderParameterT {
        +string name
        +string uniform_name
        +uint8_t uniform_offset
        +bool exposed
        +bool bounded
        +double minimum
        +double maximum
        +MaterialValueUnion default_value
    }
    class MaterialValueUnion
    ShaderDataT *-- ShaderParameterT : 独占参数声明
    ShaderParameterT *-- MaterialValueUnion : 带类型默认值
```

Shader 字节码使用 `vector<uint8_t>`，不是资源读取接口中的 `vector<std::byte>`。参数声明约束名称、Uniform 映射、可修改性、数值类型及范围；材质行为按这些声明校验。

## 资源目录

```mermaid
classDiagram
    class AssetCatalogDataT {
        +uint32_t schema_version
        +string revision
        +string target_platform
        +vector~unique_ptr~AssetEntryT~~ entries
    }
    class AssetEntryT {
        +string guid
        +string path
        +AssetKind kind
        +string content_hash
        +uint64_t content_size
        +vector~string~ dependencies
        +string module_path
    }
    class AssetKind {
        <<enum class>>
    }
    AssetCatalogDataT *-- AssetEntryT : 独占资源记录
    AssetEntryT ..> AssetKind : 资源类型
```

每个目录条目关联唯一 GUID、逻辑路径及完整性信息，依赖列表保存其他资源的 GUID；`module_path` 为脚本保留模块解析路径。运行时只消费目录和其引用的 GUID 二进制，不解析 TOML、不依赖 SQLite。
