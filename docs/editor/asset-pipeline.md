# 资源管线

[资源管线类图](class-diagrams/asset-pipeline-system.md) · [项目与出包](project.md) · [跨模块数据](../runtime/data.md)

## 源资源与身份

项目源资源位于 `assets/`。场景、网格、材质和 Shader 描述分别使用 `.scene`、`.mesh`、`.material`、`.shader` 后缀，内容为 TOML；不追加 `.toml`。TypeScript、Shader 源码、图片及其他文件保留其格式。

每个源文件都必须有邻接 `.meta`，以 TOML 保存唯一完整 GUID。新资源自动生成 meta；移动资源时一起移动 meta，保持身份；复制资源不能复用原 GUID。重复 GUID、已知资源丢失 meta 均返回失败，不静默重写身份。

核心模块的 `GuidMap` 提供逻辑路径与 GUID 的双向映射，编辑器、桌面和设备运行时共用。它不依赖 SQLite 或平台路径；逻辑路径以 `project/` 或 `builtin/` 标识来源。

## 转换与发布

`AssetPipelineSystem` 扫描源文件、校验 meta、建立依赖图，通过 `TaskSystem` 下发转换任务。任务系统使用 Taskflow，只负责依赖调度；`settings/import.toml` 的 `import_threads` 默认为 4，允许 1–64。源扫描、版本决策、文件发布和错误报告归资源管线。

转换结果写入 `cache/gen/<完整GUID>`：文件名无扩展名、类型前缀或版本子目录。`cache/catalog.bin` 保存路径、GUID、类型、依赖、大小、哈希和目标平台；`cache/assets.sqlite` 记录输入、工具及生成版本。这两个索引位于 cache 根，不进入 gen。内容哈希用于校验和增量判断，不作为文件名或资源身份。

场景、网格、材质和 Shader 描述转换为各自的 FlatBuffers 模型；TypeScript 编译为 JavaScript，Shader 源码由宿主 shaderc 编译为目标平台程序。没有专用导入器的文件按原始字节缓存，图片也遵循此规则。源码 GUID 及资源间引用由导入器验证。

首次导入转换全部资源，重复导入只转换变化及其受影响的依赖。TypeScript 指纹覆盖编译器实际读取的配置、声明、标准库和编译器文件；Shader 依赖保守包含项目全部普通源文件，避免漏掉自定义包含文件。损坏产物重新生成；删除 SQLite 后可由源文件和 meta 重建，GUID 保持不变。

导入使用项目锁、暂存输出及回滚记录。全部转换成功后才发布完整目录，失败保留上一版；无变化时不改写正式文件或版本。管线既由编辑器和 MCP 调用，也提供 `asset_build_app` 离线入口，命令见根 [README](../../README.md#资源导入与-guid)。

## 运行资源与同步

运行时只读取 catalog 和它引用的 GUID 产物，不依赖 TOML、SQLite、源 meta 或编辑器。Android 出包使用目标平台 catalog，拒绝把桌面 Shader 缓存混入 APK。

`AssetSyncSystem` 和 MCP 按完整版本同步场景、网格、材质、Shader、脚本及普通资源。校验大小与哈希后原子发布；纯资源目录无需玩法脚本也能同步。运行时启动或热重载时固定一份完整资源快照，避免不同版本混读。

热重载由运行时脚本系统执行，MCP 只负责同步和开关。材质或 Shader 改动通过完整版本重载重建场景及 GPU 资源，不是原地替换任意活跃对象。候选编译或静态链接失败保留旧实例；新入口执行后的副作用不自动回滚。

## 尚未实现的能力

文件监听自动导入、USD 场景与模型转换、纹理压缩及转码、Mesh 优化导入尚未接入资源转换流程；efsw、OpenUSD、Basis Universal 和 meshoptimizer 已参与编译、链接与依赖验证。

DCC 集成采用 USD 交换场景，专有材质与数据由 DCC 适配插件转换；贴图采用 Basis Universal 作为跨平台格式，渲染器按设备能力选择目标格式。这些扩展须通过资源管线的 GUID、依赖和完整版本契约接入。
