# 场景组件描述编解码

[类图索引](../../class-diagrams/index.md) · [场景导入](scene-importer.md)

```mermaid
classDiagram
    class SceneComponentCodec {
        <<abstract>>
        +type() ComponentPayload
        +name() string_view
        +read(const table& source, ComponentDataT& output, string& error) bool
        +validate(const ComponentDataT& data, string& error) bool
        +write(const ComponentDataT& data, table& output) void
    }
    class TransformCodec
    class CameraCodec
    class LightCodec
    class MeshRendererCodec
    class SceneImporter
    SceneComponentCodec <|-- TransformCodec
    SceneComponentCodec <|-- CameraCodec
    SceneComponentCodec <|-- LightCodec
    SceneComponentCodec <|-- MeshRendererCodec
    SceneImporter ..> SceneComponentCodec : 按类型或名称查表
```

每个组件类型在 `editor/asset_pipeline/components/` 独立文件实现 TOML 读取、载荷验证与写出。只读登记表集中列出类型，未知类型明确失败。SceneImporter 负责节点身份、层级、enabled 和原子写入；运行时 Component 不依赖 TOML。生成的 ComponentDataT 是双方共享的唯一模型。
