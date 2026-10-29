# 项目与出包模块

项目根目录固定包含资源目录 `assets/`、生成缓存目录 `cache/` 和项目设置目录 `settings/`。示例项目为 `projects/minimal/`：TypeScript 配置、package、锁文件与导入线程设置位于 settings；每个源资源旁有唯一 GUID 的 meta，生成 JS 与资源二进制统一位于 `cache/gen/<完整GUID>`，无后缀。资源目录与 SQLite 索引位于 cache 根。运行 SDK 类型声明由引擎的 [include/engine.d.ts](../../include/engine.d.ts) 统一提供。

编辑器启动传入 `--project projects/minimal`，宿主先检查根目录及三个必需子目录存在且为目录，失败不创建窗口。ProjectSystem 只负责项目目录约束；AssetPipelineSystem 读取 TS 配置并构建资源，AssetSyncSystem 发布完整运行版本，三者位于编辑器层。DCC 制作的 USD 资源及进一步离线处理属于[资源管线规划](asset-pipeline.md)。

项目是编辑器启动的必需上下文；引擎本身与 Android 运行时均不要求项目目录。`EngineConfig` 只描述引擎配置，`ProjectSystem` 由编辑器宿主持有，`EditorSystem` 借用已初始化的编辑器环境。未来的打包资源与脚本入口由游戏宿主配置，不把编辑器项目目录变成底层引擎启动条件。

项目出包由资源管线生成 GUID 产物与目录，再交由平台打包；Android 入口会启动打包项目的 Gameplay。平台专用工程与资源统一存放在 `platforms/<platform>/`，Android 使用 `platforms/android/`；构建与测试命令统一见根 [README](../../README.md)。桌面与 Android MCP 共用完整资源版本协议；本轮 GUID 管线的设备端实机验证范围见 README。

出包以单文件交付为目标，尽量将游戏运行所需内容封装在一个对应平台的可执行文件中。

引擎及游戏的构建产物以尽量小为目标，按项目需求纳入引擎模块、第三方依赖和运行资源。
