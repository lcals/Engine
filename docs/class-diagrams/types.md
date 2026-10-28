# 配置与状态

[类图索引](index.md) · [完整引用关系](references.md)

状态：本轮最小实现使用的公共数据类型。本图维护公共配置结构体和帧状态枚举，不增加行为类。

```mermaid
classDiagram
    direction TB
    class EngineConfig {
        <<struct>>
        std::string project
        std::string window_title = "Engine"
        std::uint32_t width = 1280
        std::uint32_t height = 720
    }
    class FrameStatus {
        <<enum class>>
        running
        paused
        exit_requested
        failed
    }
```

- `project` 必填，表示实际项目根目录路径；根目录及其 `settings/`、`cache/`、`assets/` 必须全部存在且为目录，否则 `Engine::initialize()` 返回 `false`。
- 当前检查文件系统目录，不接受 APK 内置资源标识；APK 内部资源访问尚未实现。
- 窗口标题不得为空，窗口尺寸不得为零或超出平台整数范围；实际像素尺寸还需通过渲染系统检查。
- 系统按需复制配置，不保存指向调用者配置的悬空引用。
- `FrameStatus` 由 `Engine::tick()` 返回；具体语义见[引擎接口约定](engine-system.md)。
