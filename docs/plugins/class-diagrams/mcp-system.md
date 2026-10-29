# MCP 工具系统

[类图索引](../../class-diagrams/index.md) · [完整引用关系](../../class-diagrams/references.md)

MCP 服务运行在桌面宿主，借用已初始化的资源管线与同步系统。目标在启动时固定；不在 Android 安装 MCP 服务。

```mermaid
classDiagram
    direction TB
    class McpTarget {
        <<struct>>
        +runtime_dir : Path
        +adb_executable : Path
        +android_serial : string
        +android_package : string
    }
    class McpSystem {
        <<final>>
        +McpSystem(AssetPipelineSystem& pipeline, AssetSyncSystem& sync, Path project_root, McpTarget target)
        +~McpSystem()
        +run_stdio() bool
        #on_initialize(const EngineConfig& config) bool
        #on_shutdown() void
        -sync_android(string& reply) bool
        -set_android_hot_reload(bool enabled, string& reply) bool
        -read_android_status(string& reply) bool
    }
    class System
    class AssetPipelineSystem
    class AssetSyncSystem
    System <|-- McpSystem
    McpSystem *-- McpTarget : 按值保存固定目标
    McpSystem --> AssetPipelineSystem : 构造借用编译管线
    McpSystem --> AssetSyncSystem : 构造借用版本生成
```

配置值和参数关系（同名节点均指向上图类型）：

```mermaid
classDiagram
    direction TB
    class McpTarget
    class McpSystem
    class Path["std::filesystem::path"]
    class String["std::string"]
    class EngineConfig
    McpTarget *-- Path : 配置路径值
    McpTarget *-- String : 设备与包名值
    McpSystem *-- Path : 项目根目录值
    McpSystem ..> McpTarget : 按值构造参数
    McpSystem ..> EngineConfig : 初始化参数
    McpSystem ..> String : Android 回执输出参数
```

文件、平台工具与协议依赖：

```mermaid
classDiagram
    direction TB
    class McpSystem
    class ResourceSystem
    class PathUtils
    class SDL3 {
        <<library>>
    }
    class Json["nlohmann/json"] {
        <<library>>
    }
    McpSystem ..> ResourceSystem : 文件与清单静态读取
    McpSystem ..> PathUtils : UTF-8 路径转换
    McpSystem ..> SDL3 : 进程参数文件流与原子替换
    McpSystem ..> Json : MCP 协议及资源清单
```

- `McpTarget` 为普通配置结构；`runtime_dir` 与 `android_serial` + `android_package` 互斥。`adb_executable` 默认 `adb`，也可明确指定完整路径。
- `McpSystem` 构造按值保存项目根目录和目标，两个系统引用须存活到 MCP 关闭。继承 `System` 生命周期，析构及关闭钩子为 `noexcept`，覆盖方法标注 `override`；返回状态的方法标注 `[[nodiscard]]`。
- 三个 Android 方法为私有实现接口，向 `reply` 输出工具回执文本，通过 `bool` 报告操作状态；不对玩法或其他系统公开新的设备控制接口。
- `Path` 与 `string` 分别简写 `std::filesystem::path` 与 `std::string`，不是新增包装类型。文件读取使用 [ResourceSystem](../../runtime/class-diagrams/resource-system.md)，编码转换使用 [PathUtils](../../runtime/class-diagrams/path-utils.md)；路径安全检查仍归调用者。
- stdio 保留 `initialize`、`notifications/initialized`、`ping`、`tools/list`、`tools/call`。保留 `sync_assets`、`set_hot_reload`、`get_status`，新增无参数 `start_project`、`stop_project`。本阶段新增启停仅面向本机编辑器；Android 沿用既有同步与热重载协议，新增启停返回明确未支持错误。
- 启停读取编辑器会话和心跳，校验项目路径后，携带请求编号原子发布 `project-control.json`；等待 `project-status.json` 对同一请求的完成回执。发布成功只表示已请求，超时明确返回未确认及请求编号，可继续查询状态；不能声称已完成。互斥发布锁防止两个 MCP 进程覆盖未确认请求。
- `get_status` 保留脚本版本字段，并合并编辑器运行状态、会话、最后请求结果、节点数和 GPU 资源数。心跳过期标记离线，磁盘历史状态不能当作活动进程证明。
- 本机目标延续持久目录协议。Android 目标必须明确 adb 序列号和包名，先核验设备状态与 `run-as` 可用性；只支持 debuggable 包。Release 不开放此调试通路。
- Android 文件位于包的私有 `files/`。同步合并设备 `assets/available.json`（若存在）及 `assets/builtin.json` 的文件和删除路径作为基线，在本机复用 `AssetSyncSystem` 生成不可变版本；APK 升级新增的内置路径也参与删除计算。
- 通过 SDL3 独立 argv 启动 adb；设备端只执行固定脚本，动态值严格校验并作 POSIX 单引号转义。逐文件传入私有暂存区，读回校验大小与哈希，最后原子发布完整版本和 `available.json`。设备发布锁防止多宿主同时操作。
- 发布前失败不修改已发布清单，不清理正在运行的版本；只清理本次拥有的暂存文件与锁。最终发布命令的回执丢失时，明确报告“发布未确认”，须查询设备状态判定，不能声称仍是旧版本。中断遗留锁会明确报告，不能自动删除他人锁。
- `set_hot_reload` 原子发布设备 `files/script-control.json`；`get_status` 直接读取设备状态和已发布清单，不以本机镜像代替设备事实。设备操作回执包含序列号和包名；目标探测成功后附持久目录。设备操作前的资产编译失败只报告编译错误。
- 可用版本表示文件已同步；运行版本及运行状态来自引擎上次报告。重载始终由引擎决定，MCP 不执行脚本生命周期。

资源同步正式调用资产任务管线完成增量编译，再以项目 cache 根的 catalog.bin 和 gen/<GUID> 发布完整设备快照；MCP 不解析数据库，也不自行实现脚本重载。Android 传输沿用相同 GUID 快照和现有 adb 事务协议，本轮只适配格式，不执行 Android APK 验证。

`sync_assets` 同步全部 catalog 条目，包括编译脚本、场景、Mesh、材质、Shader，以及原始图片/二进制等通用资源；不按脚本扩展名筛选。项目 settings、源.meta、SQLite 和未引用gen产物不进入运行版本。availableRevision 是完整资源发布版本，activeRevision 是引擎已接受的运行版本；同步发布成功不代表项目已启动或版本已经生效。纯资源 catalog 也可同步，无需 scripts/game.js；entry 仅作为后续玩法启动提示。
