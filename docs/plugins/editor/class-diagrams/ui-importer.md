# UI 文档导入

UI 是普通场景节点上的组件。该插件只把 RmlUi 文档及其显式资源引用转换成单项 UI 资源，不创建场景节点，不实现字体渲染。

```mermaid
classDiagram
    class AssetImporter
    class UiImporter {
        +id() string_view
        +kind() AssetKind
        +matches(const Path& source) bool
        +default_meta() string_view
        +inspect_batch(const ImportContext& context, string& fingerprint, string& error) bool
        +scan_dependencies(ImportAsset& asset, const ImportContext& context, string& error) bool
        +convert(const ImportAsset& asset, const ImportContext& context, string& error) bool
    }
    class AssetPipelineSystem
    class ImportAsset
    class ImportContext
    class UiDocumentData
    class UiFontBinding
    class FontData
    class TextureData
    class ShaderData
    AssetImporter <|-- UiImporter
    AssetPipelineSystem *-- UiImporter : 宿主登记后独占
    UiImporter ..> ImportAsset : 读取 rml 与相邻 meta
    UiImporter ..> ImportContext : 借用目录和输出路径
    UiImporter ..> UiDocumentData : 输出生成模型
    UiDocumentData *-- UiFontBinding : 字体族绑定列表
    UiFontBinding ..> FontData : GUID 引用
    UiDocumentData ..> TextureData : GUID 引用
    UiFontBinding ..> ShaderData : 独立 Shader 路径
```

- `.rml.meta` 的 `[ui]` 指定纹理列表；每个 `[[ui.fonts]]` 分别指定文档内字体族、字体资源和 Shader。字体族忽略 ASCII 大小写且在同一文档唯一；同一字体可通过不同族使用不同 Shader，依赖按 GUID 去重。
- 源 `.rml` 保留 RmlUi 标记和内嵌 RCSS。首阶段不接受外部样式、脚本、模板或 CSS 外部图片；`img` 仅引用 meta 已声明纹理。
- 生成的 `cache/gen/GUID` 只包含文档、字体绑定、纹理 GUID。资源依赖图负责共享加载与释放，导入器不建立独立文件加载机制。
