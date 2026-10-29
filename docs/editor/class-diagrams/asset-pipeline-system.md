# 资源管线系统

[类图索引](../../class-diagrams/index.md) · [完整引用关系](../../class-diagrams/references.md)

本轮桌面编辑器实现；独立根系统由工具宿主初始化并反序关闭。跨图引用不重复定义公开方法。

```mermaid
classDiagram
    direction TB
    class AssetPipelineSystem {
        +AssetPipelineSystem()
        +~AssetPipelineSystem()
        +build_scripts(const std::filesystem::path& project_root) bool
        +last_error() const std::string&
        #on_initialize(const EngineConfig& config) bool
        #on_shutdown() void
    }
    class System
    class EngineConfig
    class NodeJs["Node.js"]
    class TypeScript
    class USD
    class Taskflow
    class SQLite
    class efsw
    class meshoptimizer
    System <|-- AssetPipelineSystem
    AssetPipelineSystem ..> EngineConfig : 初始化参数
    AssetPipelineSystem ..> NodeJs : SDL 进程 argv 启动
    NodeJs ..> TypeScript : 编译器 API
    AssetPipelineSystem ..> USD : 编译与链接
    AssetPipelineSystem ..> Taskflow : 编译与链接
    AssetPipelineSystem ..> SQLite : 编译与链接
    AssetPipelineSystem ..> efsw : 编译与链接
    AssetPipelineSystem ..> meshoptimizer : 编译与链接
    note for AssetPipelineSystem "宿主拥有；settings 配置、assets 源码、cache 产物；编译失败不发布"
    class PathUtils
    AssetPipelineSystem ..> PathUtils : UTF-8 路径转换
```

通过 SDL3 进程 API 传递独立 argv，调用固定构建脚本；Node 和构建脚本由构建配置指定。TS 配置放 settings，源码 assets，编译成功后替换 cache/assets。失败返回 false，并保留上次有效产物；调用方不能在 false 后同步旧产物。保留 USD、Taskflow、SQLite、efsw、meshoptimizer 的实际编译链接探针。`last_error()` 返回借用错误描述。

返回状态及只读访问方法标注 `[[nodiscard]]`；析构和清理为 `noexcept`，钩子标注 `override`。`last_error()` 为 const noexcept，只读字符串借用在下一次业务操作前有效。

公共路径编码复用 [PathUtils](../../runtime/class-diagrams/path-utils.md)；路径规范化和安全检查仍由本类负责。
