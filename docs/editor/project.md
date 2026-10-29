# 项目与出包模块

项目根目录固定包含资源目录 `assets/`、生成缓存目录 `cache/` 和项目设置目录 `settings/`。示例项目为 `projects/minimal/`：TypeScript 配置、package 与锁文件位于 settings，项目入口源码为 `assets/scripts/game.ts`，生成 JS 和资源镜像位于 `cache/assets/`。运行 SDK 类型声明由引擎的 [include/engine.d.ts](../../include/engine.d.ts) 统一提供。

编辑器启动传入 `--project projects/minimal`，宿主先检查根目录及三个必需子目录存在且为目录，失败不创建窗口。ProjectSystem 只负责项目目录约束；AssetPipelineSystem 读取 TS 配置并构建资源，AssetSyncSystem 发布完整运行版本，三者位于编辑器层。DCC 制作的 USD 资源及进一步离线处理属于[资源管线规划](asset-pipeline.md)。

项目是编辑器启动的必需上下文；引擎本身与 Android 运行时均不要求项目目录。`EngineConfig` 只描述引擎配置，`ProjectSystem` 由编辑器宿主持有，`EditorSystem` 借用已初始化的项目。未来的打包资源与脚本入口由游戏宿主配置，不把编辑器项目目录变成底层引擎启动条件。

项目出包规划为由资源管线生成资源清单，再交由平台打包；当前 Android 最小引擎包不加载项目玩法资源。平台专用工程与资源统一存放在 `platforms/<platform>/`，Android 使用 `platforms/android/`；构建与测试命令统一见根 [README](../../README.md)。桌面编辑器和 MCP 已能发布完整版本，Android 同步接入仍待后续实施。

出包以单文件交付为目标，尽量将游戏运行所需内容封装在一个对应平台的可执行文件中。

引擎及游戏的构建产物以尽量小为目标，按项目需求纳入引擎模块、第三方依赖和运行资源。
