# 渲染编辑命令

[类图索引](../../class-diagrams/index.md)

```mermaid
classDiagram
    class RenderCommands {
        +RenderCommands(RenderSystem& renderer, Path capture_root)
        +register_commands(EditorCommands& commands) bool
    }
    class RenderSystem
    class EditorCommands
    class EditorEdit
    RenderCommands --> RenderSystem : 借用渲染接口
    RenderCommands ..> EditorCommands : 注册显示与截图命令
    RenderCommands ..> EditorEdit : 渲染设置撤销记录
```

编辑器决定截图目录并检查相对路径；渲染器只接收明确的文件路径。渲染设置支持撤销；截图是异步请求，回执中的 pending 不代表文件完成，get_frame_capture 返回真实完成状态。

宿主独占 RenderCommands，命令注册和撤销历史释放之后销毁它；渲染系统的有效期覆盖命令对象。
