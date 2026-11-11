# 运行程序的 MCP 测试接入

独立测试宿主通过可选 runtime/mcp 插件装配 RuntimeMcpEndpoint，编辑器通过 editor/mcp 装配 EditorMcpEndpoint。端点复用同一协议和通用命令通道，运行时核心不依赖编辑器或具体插件。

方法、状态、工具、构建隔离与请求回执的唯一契约见[运行时 MCP 与场景测试](../../runtime/class-diagrams/mcp-runtime-test.md)；调用方式见[MCP 使用说明](../mcp.md#独立运行程序测试)。
