# 项目与出包模块

项目根目录固定包含资源目录 `assets/`、生成缓存目录 `cache/` 和设置目录 `settings/`。示例为 `projects/showcase/`：TypeScript 配置、package、锁文件与导入线程设置位于 settings；每个源资源旁有唯一 GUID 的 meta，生成 JavaScript 与资源二进制位于 `cache/gen/<完整GUID>`，无后缀。资源目录与 SQLite 索引位于 cache 根。运行 SDK 声明统一位于引擎 [include/engine.d.ts](../../include/engine.d.ts)。

编辑器启动必须传入项目路径。宿主先检查根目录及三个子目录存在且为目录，失败不创建窗口。`ProjectSystem` 负责目录约束；`AssetPipelineSystem` 读取设置并构建资源；`AssetSyncSystem` 发布完整版本。三者都属于编辑器层。`EditorEnvironment` 借用项目，准备 App 路径并向核心提交完整导入目录，供 `EditorSystem` 使用。

`Engine` 只管理抽象系统的生命周期与调度，不要求项目目录；运行时装配由 `RuntimeHost` 完成，玩法会话由 `GameplaySession` 管理。Android 宿主配置包内与持久路径后启动 Gameplay，不携带或校验编辑器项目目录。

出包时资源管线生成目标平台 catalog 与 GUID 产物，平台工具将其部署到包内。平台专用工程、入口与资源集中于 `platforms/<platform>/`；Android 位于 `platforms/android/`。SQLite、源 meta、TypeScript 工程设置不进入运行包，构建与测试命令见根 [README](../../README.md)。

桌面与 Android 调试包共用完整资源版本协议。既有 Android 打包入口、V8、PBR 场景、资源同步及生命周期已有模拟器验证记录；资源目录 v2 与合并包的最新验证范围见 README。发布签名、正式内容更新渠道与其他平台的单文件交付尚未实现。
