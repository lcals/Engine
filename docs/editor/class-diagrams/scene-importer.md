# 场景导入与保存

[类图索引](../../class-diagrams/index.md)

```mermaid
classDiagram
    class SceneImporter {
        +read(const filesystem_path& source, SceneDataT& output, string& error)$ bool
        +write(const filesystem_path& destination, const SceneDataT& data, string& error)$ bool
    }
    class SceneDataT
    class AssetPipelineSystem
    class SceneComponentCodec {
        <<interface>>
        +scan_dependencies(const ComponentDataT& component, const ImportContext& context, vector~Guid~& dependencies, string& error) bool
    }
    class ImportContext
    class SceneAssetImporter
    SceneAssetImporter ..> SceneComponentCodec : 逐组件扩展依赖
    SceneComponentCodec ..> ImportContext : 借用本批次目录
    SceneImporter ..> SceneDataT : FlatBuffers 唯一数据模型
    AssetPipelineSystem ..> SceneImporter : 转换读取
```

源 `.scene` 是 TOML。保存与读取均验证局部稳定节点 ID、父子关系、唯一组件、唯一 Transform、活动相机及资源 GUID；不把运行时实体号直接写入源文件。读失败清空输出；写入采用同目录临时文件后替换，失败保留已有源文件。引用存在性由管线依赖分析再验证。

相机保存透视/正交投影及正交竖直半高；灯光保存方向/点/聚光类型、范围及内外半角。MeshRenderer 保存槽 0 的 `material` 和槽 1 起的 `additional_materials` GUID 数组，全部进入场景依赖。旧场景未写新增字段时采用生成数据模型的默认值。

组件 Codec 可覆写 `scan_dependencies`，向给定依赖列表追加已经校验的 GUID；默认方法不追加。场景管线统一调用登记的 Codec，插件自己负责其组件引用的资源类型验证，编辑器不认识具体插件类型。现有内置组件引用收集保持原语义。
