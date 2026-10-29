# 项目系统

[类图索引](index.md) · [引擎与系统](engine-system.md) · [完整引用关系](references.md)

状态：本轮最小实现。仅定义 `ProjectSystem` 的方法；其他节点为跨图引用或第三方类型。

```mermaid
classDiagram
    direction TB
    class ProjectSystem {
        <<final>>
        +ProjectSystem(std::string project_root)
        +~ProjectSystem()
        -std::string project_root_
        #on_initialize(const EngineConfig& config) bool
        #on_shutdown() void
    }
    class System {
        <<abstract>>
    }
    class EngineConfig {
        <<struct>>
    }
    class tomlplusplus["toml++"] {
        <<library>>
    }
    System <|-- ProjectSystem
    ProjectSystem ..> EngineConfig : on_initialize 参数
    ProjectSystem ..> tomlplusplus : 编译及链接依赖
    note for ProjectSystem "继承 System 生命周期；析构中调用 shutdown()"
```

- 编辑器宿主独立拥有的根系统；运行时 `Engine` 不拥有或依赖它。构造函数接收实际项目根目录并保存自己的字符串副本，不借用调用者字符串。
- 构造函数只保存路径；`on_initialize()` 检查根目录及其 `settings/`、`cache/`、`assets/` 均存在且为目录；空路径、任一目录缺失、类型不符或无法完成检查均返回 `false`。继承的配置参数保留统一生命周期签名，当前不读取其中的窗口配置。
- 文件系统检查使用返回状态和 `std::error_code`，不使用异常。编辑器宿主先初始化项目，再启动引擎窗口和编辑器系统；项目验证失败时编辑器启动失败，尚未初始化窗口。
- toml++ 继续参与项目配置数据的编译与链接验证。当前完成目录结构检查，具体配置文件读取以及 APK 内部资源访问尚未实现。
- 公开业务接口当前只有构造与析构；继承 `System` 的公共生命周期方法，不提前增加业务方法。初始化失败和清理遵守[系统约定](engine-system.md)。
- 不重写 `on_tick()`，沿用基类默认成功的空更新；当前无逐帧业务任务。

C++ 限定符：构造为 `explicit`；析构及 `on_shutdown()` 为 `noexcept`；析构和受保护钩子标注 `override`，返回状态的钩子标注 `[[nodiscard]]`。
