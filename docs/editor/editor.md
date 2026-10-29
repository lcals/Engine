# 编辑器模块

编辑器使用 C++ 与 Dear ImGui docking，依赖运行时能力；运行时不反向依赖编辑器。项目路径由宿主先校验，`EditorEnvironment` 集中配置 App 路径并准备会话内置资源快照，`EditorSystem` 借用已就绪的环境和玩法会话。

编辑器建立 SDL3 窗口及 ImGui 上下文，默认空白待机。MCP `start_project` 启动项目，在同一窗口显示场景；`stop_project` 结束玩法并恢复空白。会话负责释放场景和项目 GPU 资源，窗口与图形设备保持存活。项目控制在编辑器主线程执行，状态通过持久运行目录对外发布，详见根 [README](../../README.md#渲染与项目控制)。

编辑器功能通过 MCP 提供资源导入、完整版本同步、玩法启停、热重载开关和状态查询。脚本及资源重载由运行时执行，MCP 不实现重载。模型、业务控制和界面呈现须分别归属对应模块，界面调用已有能力，不复制业务实现。

完整编辑器界面、多窗口布局、C++ 编辑器模块与 TypeScript 编辑器插件热重载尚未实现。桌面目标覆盖 macOS、Windows 和 Linux；Dear ImGui Multi-Viewports 的 Wayland 限制与部分 X11 窗口管理器兼容性须在接入多窗口时验证。
