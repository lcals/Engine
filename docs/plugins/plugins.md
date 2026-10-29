# 脚本与插件模块

本文保留插件扩展的规划目标。当前代码按职责分层：V8 玩法脚本系统位于 `runtime/script/`；MCP 工具位于 `plugins/mcp/`，调用 `editor/` 中的资源管线与同步能力，不把 MCP 协议放进运行时。

已实现接口见[运行时脚本系统](../runtime/class-diagrams/script-system.md)、[项目玩法入口](../runtime/class-diagrams/gameplay.md)和 [MCP 系统](class-diagrams/mcp-system.md)。当前热重载重建玩法 Context 与 Game，不保留任意 JavaScript 状态；插件多线程和状态迁移属于下文的后续目标。

引擎主体与编辑器使用 C++；游戏脚本、编辑器插件和运行插件使用 TypeScript。TypeScript 编译为 JavaScript 后由 [V8](https://v8.dev/) 执行。

参考熟悉的脚本接口使用方式：引擎能力由 C++ 实现，并通过稳定的 TypeScript API 供脚本与插件调用，公开声明统一维护在 [include/engine.d.ts](../../include/engine.d.ts)。未来场景脚本采用 Node 与普通 Component，不增加 Behaviour 或 MonoBehaviour 层次。后续状态迁移需明确可保存的数据与恢复规则，再支持跨重载延续；不能把该目标当作当前任意状态回滚承诺。

插件系统与编辑器并列，编辑器可以调用插件。插件分为编辑器插件和运行插件，分别运行在对应宿主中；脚本与插件支持多线程执行，由宿主管理执行与隔离边界。

游戏运行时脚本与运行插件的长期目标是无需重启引擎即可切换逻辑，并支持明确约定的运行数据保留。当前实现只在候选模块编译或静态链接失败时保留旧运行；新入口开始执行后的业务副作用不能自动回滚。引擎负责热重载，MCP 负责同步并控制开关，支持通过增补日志和调整逻辑快速调试。

编辑器代码与插件的热重载目标见[编辑器模块](../editor/editor.md)。
