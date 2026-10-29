# 编辑器模块

编辑器使用 C++ 实现。编辑器设计参考现代游戏引擎，支持 macOS、Windows、Linux 和多个独立系统窗口；相关代码使用统一的桌面平台宏进行条件编译。GUI 先采用 Dear ImGui（docking 分支）。

编辑器采用 MVC：Model 管理数据，View 负责界面呈现，Controller 处理用户操作；业务逻辑与 Dear ImGui 界面代码分离。引擎提供 MCP 服务，编辑器功能先实现为 MCP 接口，再将对应能力接入 UI。

编辑器必须支持代码热重载，覆盖 C++ 编辑器模块与 TypeScript 编辑器插件。代码修改并构建成功后，无需重启编辑器即可更新相关功能；构建或加载失败时保留当前版本并报告错误。C++ 原生模块的可重载范围与跨平台实现方式在实现阶段确定。

多窗口是编辑器目标要求。[Dear ImGui 官方说明](https://github.com/ocornut/imgui/wiki/Multi-Viewports)指出，其 Multi-Viewports 在 Linux Wayland 下不工作，在部分 X11 窗口管理器下也有问题；Linux 多窗口方案需要单独设计和验证。

编辑器 Play 模式的材质、贴图与 Shader 热重载目标见[渲染模块](../runtime/rendering.md)。
