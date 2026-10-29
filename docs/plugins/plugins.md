# 脚本与插件模块

V8 玩法脚本位于 `runtime/script/`；MCP 工具位于 `plugins/mcp/`，调用编辑器资源管线和同步能力。运行时不依赖 MCP 协议。相关接口见[脚本系统](../runtime/class-diagrams/script-system.md)、[玩法入口](../runtime/class-diagrams/gameplay.md)和 [MCP 系统](class-diagrams/mcp-system.md)。

引擎主体与编辑器使用 C++，玩法使用 TypeScript。TypeScript 编译为 JavaScript 后由 V8 执行，原生能力通过 `engine` ES 模块暴露，公开声明统一维护于 [include/engine.d.ts](../../include/engine.d.ts)。每个绑定类实现统一接口并在注册表登记，绑定外观和原生回调归对应类的实现文件。

项目 `Gameplay` 是场景之外的入口，可以承载业务与场景切换；场景对象使用 Node 和普通 Component，不增加 Behaviour 或 MonoBehaviour 层次。脚本运行和回调由宿主线程驱动。

MCP 提供项目启停、资源导入、完整版本同步、热重载开关与状态查询。同步覆盖 catalog 中的全部资源；桌面通过持久目录发布，Android 调试包通过明确指定设备的 adb 通路发布。MCP 不直接重载 V8，也不安装设备端 MCP 服务。

McpSystem 只处理协议和工具表。ProjectMcpTools 与 EditorMcpTools 在初始化前自行登记工具描述、参数模型及回调，协议层不持有资源系统或项目路径。桌面场景工具通过文件通道进入编辑器主线程，由 EditorCommands 调用 SceneCommands，支持现有组件修改、批量回滚和撤销重做。组件提供普通 C++ 方法，不实现 MCP 接口；查询或修改能力由所属业务模块决定。场景编辑工具不进入 Android 运行时。

工具层的完成回执、主线程编辑记录与运行时对象分开管理。请求带完整项目、会话和编号；超时保留可查询身份，已执行请求不会因重复提交而再次修改场景。停止或场景切换使历史失效。公开契约见[编辑器 MCP 工具](class-diagrams/editor-mcp-tools.md)与[编辑器命令](../editor/class-diagrams/editor-commands.md)，使用命令和限制见根 README。

热重载重建玩法 Context 和实例。候选编译或静态链接失败时保留旧运行；新入口开始执行后，业务副作用不自动回滚。关闭热重载仍可同步待应用版本，停止项目后不会因同步自动启动。

独立编辑器插件、运行插件、多线程插件执行、C++ 模块热重载及状态迁移尚未实现。扩展接口须明确宿主、生命周期、线程和状态边界，不能将这些能力视为已有 V8 热重载的保证。
