# 编辑器插件扩展工具

[类图索引](../../class-diagrams/index.md) · [资源管线](asset-pipeline-system.md) · [组件命令](component-commands.md)

公开头文件位于 `include/engine/editor/`，实现仍归编辑器。插件通过这些函数复用能力，不包含编辑器私有头文件；不增加 Manager、镜像状态或通用插件基类。

| 公开头文件 | 职责与约束 |
| --- | --- |
| `asset_pipeline/asset_io.hpp` | 内容指纹、字节读写、项目内路径校验、源文件和 meta 原子创建及 GUID 生成。指纹复用核心哈希函数；项目策略和锁的取得仍由调用方决定。 |
| `asset_pipeline/import_tool.hpp` | 唯一工具进程入口和影片工具定位。以 argv 执行并回收本次进程，合并 stdout/stderr，返回成功、失败或已回收取消；调用方决定时限和输出上限。 |
| `asset_pipeline/importer_utils.hpp` | 使用 ImportContext 读取生成载荷、校验资源引用、解析描述和工具/Schema 指纹。材质参数校验、可选脚本配置指纹留在编辑器内部；不暴露数据库、增量队列或发布事务。 |
| `asset_pipeline/toml_helpers.hpp` | TOML 值到已有生成模型的类型与范围检查，不持有项目状态。 |
| `asset_pipeline/project_lock.hpp` | ProjectLock 独占系统锁，acquire/try_acquire 返回状态，析构释放；不复制、不移动。 |
| `asset_pipeline/editor_tools.hpp` | 已部署工具和数据定位，以及 Shader 目标选择；平台目录探测留在私有实现。 |
| `file_text.hpp` | 物理源文件读取为文本，失败清空输出，不加载运行资源。 |
| `resource_access.hpp` | 编辑器显式准备资源、保活引用和捕获版本元数据；不建立共享缓存或第二套引用计数。 |
| `mesh/mesh_lods.hpp`、`mesh/mesh_tangents.hpp` | 生成离线 LOD/切线，使用生成 Mesh 数据；LOD 参数解析结构留在实现内。 |

文件和资源借用只在调用期间有效；返回的拥有值与 ResourceReference 由调用方管理。失败通过 bool 或所属状态枚举及已有错误输出报告，必要资源在失败路径释放。插件继续由宿主显式登记，编辑器不反向链接任何插件。

```mermaid
classDiagram
    class EditorPlugin {
        <<caller>>
    }
    class ImportContext
    class ResourceLoader
    class ResourceReference
    class ProjectLock {
        <<final>>
        +ProjectLock()
        +~ProjectLock()
        +acquire(const path& file, string& error) bool
        +try_acquire(const path& file, bool& acquired, string& error) bool
    }
    class asset_pipeline {
        <<namespace functions>>
        +hash_bytes(span~const byte~ bytes) string
        +hash_text(const string& text) string
        +read_bytes(const path&, vector~byte~& bytes, string& error) bool
        +write_bytes(const path&, span~const byte~ bytes, string& error) bool
        +write_text(const path&, const string& text, string& error) bool
        +project_file_path(const path& project, string_view directory, string_view relative, string_view extension, path& output, string& error) bool
        +create_source_with_meta(const path& destination, const string& source, string_view importer, string& guid, string& error) bool
        +find_import_tool(string_view name, path& output, string& error) bool
        +run_import_tool(const vector~string~& arguments, string& output, string& error, const CancellationCheck& cancel_requested = {}, size_t max_output_bytes = SIZE_MAX) ImportToolStatus
        +new_guid() string
        +read_generated_payload(const Guid&, const ImportContext&, vector~byte~& output, string& error) bool
        +read_table(const path&, toml_table& table, string& error) bool
        +reference(const Guid&, AssetKind, const ImportContext&, vector~Guid~& dependencies, string& error) bool
        +file_hash(const path&, string& hash, string& error) bool
        +schema_fingerprint(initializer_list~string_view~ names, string& fingerprint, string& error) bool
        +source_extension(const path&) string
        +generic_path(const path&) string
        +shader_target(string_view target, string& platform, string& profile) bool
        +editor_tool_path(string_view name, const path& fallback, path& output, string& error) bool
        +editor_tool_data_path(string_view relative, const path& fallback, path& output, string& error) bool
        +identifier(const toml_node* node, uint64_t& output) bool
        +number(const toml_node* node, double& output) bool
        +numbers(const toml_node* node, array& output) bool
        +float_number(const toml_node* node, float& output) bool
        +value_union(const toml_table& table, MaterialValueUnion& output, string& error) bool
    }
    class resource_access {
        <<namespace functions>>
        +load(ResourceLoader*, string_view path_or_guid, ResourceReference& output) bool
        +load_versions(ResourceLoader*, span~const AssetEntryT~ versions, vector~ResourceReference~& output) bool
        +collect_versions(const ResourceReference& root, vector~AssetEntryT~& output) bool
        +get(ResourceLoader*, ResourceHandle) const Resource*
        +guid(ResourceLoader*, string_view path, string& output) bool
        +prepare_guid(ResourceLoader*, string_view path, string& output, vector~ResourceReference~& prepared) bool
        +path(ResourceLoader*, string_view guid, string& output) bool
    }
    class file_detail {
        <<namespace functions>>
        +read_text(const path&, string& output) bool
    }
    class editor {
        <<namespace functions>>
        +generate_mesh_lods(MeshDataT&, const toml_table& meta, string& error) bool
        +generate_mesh_lods(MeshDataT&, const path& source, string& error) bool
        +mesh_lod_fingerprint() string
        +generate_mesh_tangents(MeshDataT&, string& error) bool
    }
    class Resource
    class ResourceHandle
    class AssetEntryT
    class AssetKind
    class MaterialValueUnion
    class MeshDataT
    class CancellationCheck {
        <<type alias>>
    }
    class ImportToolStatus {
        <<enumeration>>
        success
        failed
        cancelled
    }
    EditorPlugin ..> asset_pipeline : 显式调用
    EditorPlugin ..> resource_access : 显式准备
    EditorPlugin ..> file_detail : 源文本
    EditorPlugin ..> editor : 离线网格
    asset_pipeline ..> ImportContext : 本次借用
    asset_pipeline ..> AssetKind : 参数
    asset_pipeline ..> MaterialValueUnion : 生成模型输出
    asset_pipeline ..> CancellationCheck : 本次调用借用
    asset_pipeline ..> ImportToolStatus : 返回进程终态
    resource_access ..> ResourceLoader : 可空借用
    resource_access ..> ResourceReference : 输出拥有引用
    resource_access ..> ResourceHandle : 输入值
    resource_access ..> Resource : 返回借用
    resource_access ..> AssetEntryT : 独立版本元数据
    editor ..> MeshDataT : 本次修改
    EditorPlugin *-- ProjectLock : 显式锁作用域
```

ProjectLock 属于 `engine::editor::asset_pipeline`。图中命名空间均相对 `engine::editor`；`editor` 节点表示该命名空间自身的网格函数。`path` 为 `std::filesystem::path`，`toml_table/toml_node` 为 `toml::table/node`，`numbers<Count>` 的输出是 `std::array<double, Count>`。这些简写不对应新增 C++ 类型。`get` 返回资源借用，只能在加载器及对应获取有效期间使用；`load` 成功替换输出拥有引用，失败保留原输出，局部候选自动释放。

`run_import_tool` 为调用线程内的同步操作，不建立工作线程或嵌套任务。调用开始清空 output/error；成功返回合并输出，失败返回原因和已捕获诊断，取消只在未启动或已回收进程后返回。输出超过正值上限时停止并回收本次进程；默认 `std::numeric_limits<size_t>::max()` 保持原同步工具没有额外上限的行为。取消回调为可空 `std::function<bool()>`，借用持续到调用结束；时限由调用方组合取消条件。`run_process` 已被替换，不保留转发壳或旧签名。
