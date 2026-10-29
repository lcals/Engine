# 建模资源导入

建模导入属于 `engine::plugins::modeling`。宿主把插件实现注入编辑器资源管线；编辑器及运行时均不依赖建模插件。

```mermaid
classDiagram
    class AssetImporter
    class MeshImporter
    class ModelingMeshImporter {
        +inspect_batch(const ImportContext& context, string& fingerprint, string& error) bool
        +convert(const ImportAsset& asset, const ImportContext& context, string& error) bool
    }
    class AssetPipelineSystem
    class EditableMesh
    class ImportAsset
    class ImportContext
    class EditableMeshDataT
    class MeshDataT
    AssetImporter <|-- MeshImporter
    MeshImporter <|-- ModelingMeshImporter
    AssetPipelineSystem *-- AssetImporter : 宿主注入后独占
    ModelingMeshImporter ..> EditableMesh : 校验并编译权威建模源
    ModelingMeshImporter ..> ImportAsset : 借用源身份
    ModelingMeshImporter ..> ImportContext : 借用批次上下文
    ModelingMeshImporter ..> EditableMeshDataT : 反序列化可编辑拓扑
    ModelingMeshImporter ..> MeshDataT : 生成普通运行Mesh
```

`inspect_batch` 加入普通 Mesh Schema、建模 Schema 和插件转换版本；注入、移除或格式改变都会使旧缓存失效。`convert` 为 const，只读批次输入，失败不发布产物。普通 `.mesh` 调用基类转换；含 `[modeling]` 的资源必须验证并反序列化权威 FlatBuffers 数据，再编译运行 Mesh，不能使用冗余渲染数组。插件不在正式运行包内，包内只包含普通 Mesh 实体。

目标 `engine::modeling_importer` 向下链接编辑器资源管线和建模几何库；宿主决定是否链接及注入。新增建模功能不修改编辑器的资源分派。
