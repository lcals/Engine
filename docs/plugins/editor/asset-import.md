# 统一资源导入

`editor/asset_import` 是离线源格式导入插件，统一包含 FBX、USD、字体、UI 文档与 JSON 的五种导入器。插件合并构建和装配边界，各类型继续独立文件、importer id、meta 参数、工具指纹与测试。

| 项目 | 约定 |
| --- | --- |
| CMake 选择 | `ENGINE_EDITOR_PLUGINS` 中的 `asset_import` |
| CMake 目标 | `engine_asset_import`，别名 `engine::asset_import` |
| 命名空间 | `engine::plugins::asset_import` |
| 公开头 | `include/engine/plugins/editor/asset_import/` |
| 实现 | `src/engine/plugins/editor/asset_import/` |

资源管线开启时，默认清单选择本插件；已有构建使用显式清单时，应将原五个格式插件名替换为 `asset_import`，保留其他已选插件。旧格式插件目标、命名空间和目录不再保留。完整配置方法见[插件选择](../plugins.md#插件选择)。

导入器继续实现编辑器基础层的 `AssetImporter`。编辑器、资源构建和 MCP 工具宿主逐类型构造，再通过 `AssetPipelineSystem::add_importer()` 移交给登记表；不增加统一注册门面、System 或 Manager。扫描、任务、GUID、缓存、依赖和原子发布继续由[资源管线](../../editor/asset-pipeline.md)负责。

| 导入器 | 类型与参数说明 |
| --- | --- |
| FbxImporter / UsdImporter | [模型导入](model-import.md)，各自的[FBX](class-diagrams/fbx-importer.md)与[USD](class-diagrams/usd-import.md)契约 |
| FontImporter | [字体导入参数](font-import.md) |
| UiImporter | [UI 文档与依赖](ui-import.md) |
| JsonImporter | [JSON 格式约束](class-diagrams/json-importer.md)与[项目配置读取](../runtime/scripting.md#json-配置资源) |

ufbx、OpenUSD、FreeType 和格式校验依赖由本插件目标组织，不进入运行包。`font-import`、`usd-import` 仍是 vcpkg 依赖 feature 名称，不是可单独选择的编辑器插件。UI 编辑插件 `editor/ui` 依赖本插件，复用 UiImporter 的源文档校验；运行时 UiSystem 和 FontSystem 仍属于核心。

建模、地形、破坏与特效插件还承担制作、组件编解码及编辑命令，保持各自边界；它们的导入适配继续通过相同接口登记。运行时只消费按标准 GUID 管线生成的独立资源。

架构见[统一导入类图](class-diagrams/asset-import.md)，构建及功能验收见[测试文档](../../testing.md)。
