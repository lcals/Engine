# 资源管线

[资源管线类图](class-diagrams/asset-pipeline-system.md) · [项目与出包](project.md) · [跨模块数据](../runtime/data.md)

导入收集完整 `assets/` 信息并生成增量缓存；[资源构建系统](../plugins/editor/resource-build.md)根据打包清单选择运行子集。构建依赖与运行依赖分开维护，编辑器直接把成功导入的目录提交给核心，无需先打包才能运行项目。

## 源资源与身份

项目源资源位于 `assets/`。场景、网格、材质和 Shader 描述分别使用 `.scene`、`.mesh`、`.material`、`.shader` 后缀，内容为 TOML；不追加 `.toml`。TypeScript、Shader 源码、图片及其他文件保留其格式。

每个源文件都必须有邻接 `.meta`，以 TOML 保存唯一完整 GUID。新资源自动生成 meta；移动资源时一起移动 meta，保持身份；复制资源不能复用原 GUID。重复 GUID、已知资源丢失 meta 均返回失败，不静默重写身份。

核心模块的 `GuidMap` 提供逻辑路径与 GUID 的双向映射，编辑器、桌面和设备运行时共用。它不依赖 SQLite 或平台路径；逻辑路径以 `project/` 或 `builtin/` 标识来源。

## 转换与发布

`AssetPipelineSystem` 扫描源文件、校验 meta、建立依赖图，通过 `TaskSystem` 下发转换任务。任务系统使用 Taskflow，只负责依赖调度；`settings/import.toml` 的 `import_threads` 默认为 4，允许 1–64。源扫描、版本决策、文件发布和错误报告归资源管线。

系统属于 `engine::editor`，逐类型导入接口与实现属于 `engine::editor::asset_pipeline`。每种资源在独立导入器文件中实现识别、默认 meta、指纹、依赖与转换；管线只通过登记接口调度。初始化登记默认实现，宿主可在空闲时通过 `add_importer` 登记新 id，或通过 `set_importer` 替换已有 id 的实现，关闭后清空。编辑器基础不引用或链接具体插件。

转换结果写入 `cache/gen/<完整GUID>`：文件名无扩展名、类型前缀或版本子目录。每个文件是 `ERS1` 资源实体，包含带 `ERSH` 标识的 FlatBuffers 实体头与载荷。`cache/catalog.bin` 使用版本 2，保存 GUID、逻辑路径、类型、必要运行依赖、完整实体大小和哈希、头长度、存储定位及目标平台；导入缓存的定位为 `content` 挂载下的 `gen/<GUID>`。

`cache/assets.sqlite` 使用版本 2，记录源、工具、配置和生成指纹；`dependencies` 表用于构建失效和任务排序，`runtime_dependencies` 表用于打包闭包。数据库与目录索引位于 cache 根，不进入 gen。实体哈希用于校验和增量判断，不改变 GUID 身份。

场景、网格、材质和 Shader 描述转换为各自的 FlatBuffers 载荷；TypeScript 编译为 JavaScript，Shader 源码由宿主 shaderc 编译为目标平台程序。PNG/JPG/JPEG 通过 [TextureImporter](class-diagrams/texture-importer.md) 生成包含 BasisU 数据的纹理载荷，格式、压缩、颜色空间和 mip 设置取自相邻 `.meta`。没有专用导入器的文件保留原始字节作为载荷。转换结束后统一调用核心实体编码接口封装，不能把 `gen/<GUID>` 直接当作裸 JS 或裸 FlatBuffer 读取。

建模插件的 [ModelingMeshImporter](../plugins/editor/class-diagrams/modeling-mesh-importer.md) 由编辑器、构建 CLI 和 MCP 宿主显式注入。它从 `.mesh` 的 `[modeling]` 权威数据编译普通运行 Mesh；基础 MeshImporter 仍处理普通网格。未注入插件时，权威源导入明确失败，不使用冗余渲染数组。导入器身份和 Schema 参与增量指纹，移除插件不能绕过检查复用旧缓存。

首次导入转换全部资源，重复导入只转换变化及其受影响的依赖。TypeScript 指纹覆盖编译器实际读取的配置、声明、标准库和编译器文件；实际生成 JS 中的模块 import / export 和字面量动态 import 形成运行依赖，类型导入被剔除。Shader 的构建依赖保守包含项目全部普通源文件以覆盖自定义 include，这些源码不会自动成为 Shader 的运行依赖。

损坏产物重新生成，缓存命中也校验完整实体头、大小和哈希。删除 SQLite 后可由源文件和 meta 重建，GUID 保持不变。普通源资源仍有 meta、实体和完整编辑器目录条目，可通过打包入口或保留项显式选入运行集合。

导入使用项目锁、暂存输出及回滚记录。转换结束后调用核心 `ResourceCatalog.assign()` 校验完整目录；缺失运行依赖、运行依赖环、非法实体元数据等都在发布前失败。全部成功后发布目录，失败保留上一版；无变化时不改写正式文件或版本。

`AssetPipelineSystem.catalog()` 返回最近成功导入的完整内存目录，只包含元数据；下一次成功导入更新内容，失败保留旧目录，关闭时清空。编辑器协调层使用该数据及 `content` 挂载直接调用核心 `update_catalog()`。运行中的编辑器项目会阻止覆盖同一 `cache/gen/`；调用方必须先停止，导入工具不自动停启。管线也提供 `asset_build_app` 离线入口，命令见根 [README](../../README.md#资源导入与-guid)。

## 格式插件与子资源

[FBX](../plugins/editor/class-diagrams/fbx-importer.md)与[USD](../plugins/editor/class-diagrams/usd-import.md)均为可选编辑器插件。`AssetImporter::id()` 描述源格式，`kind()` 描述主产物；多个格式可以输出 `model`。插件 `discover` 返回稳定子资源 key/kind，管线在源 meta 的 `[[subresources]]` 分配并保留 GUID。主产物及每个子产物各有目录记录、构建键和独立 `gen/GUID`，主 Model 用 GUID 组织节点和依赖。

已消失的子资源不会进入本批运行目录，但身份保留在源 meta 并参与全项目冲突检查。源文件、meta、Schema 或导入器指纹变化按依赖传播；没有变化时整批复用，不改写正式版本。发现阶段已知子资源丢失身份时失败，防止引用被悄悄重分配。

FBX 生成层级、几何、骨架和采样动画；USD 当前只生成自包含的静态凸多边形模型。具体格式限制由各插件校验。核心只定义运行 Schema、ResourceCodec 和场景实例化，格式依赖不进入运行包；MCP 调用现有导入/构建/同步能力，无需复制每种格式的算法。

## 运行资源与同步

运行时读取 catalog 元数据，并在使用时读取所需资源，不在启动时把所有实体读入内存；可从独立 GUID 文件或合并包中的区间读取。运行时不依赖 TOML、SQLite、源 meta 或编辑器。Android 出包使用目标平台 catalog，拒绝把桌面 Shader 缓存混入 APK。

`AssetSyncSystem` 和 MCP 按完整运行清单同步场景、网格、材质、Shader、脚本及普通资源，以及它们引用的独立文件或包。校验后发布对应版本；纯资源目录无需玩法脚本也能同步。版本边界由调用方提供的目录和存储挂载确定，核心不扫描目录或自动切换版本。真机启动提交一次目录，正常运行使用固定集合。

热重载由 GameplaySession 协调脚本停止、资源释放、目录提交和重新运行，MCP 只负责同步和开关。材质或 Shader 改动通过完整版本重载重建场景及 GPU 资源，不是原地替换任意活跃对象。候选编译或静态链接失败保留旧实例；新入口执行后的副作用不自动回滚。

## 尚未实现的能力

文件监听自动导入、完整 USD 场景语义和 Mesh 优化导入尚未接入。基础管线保留 efsw/meshoptimizer 依赖验证，OpenUSD 已转入独立格式插件；Basis Universal 的编辑器压缩与运行时转码已接入贴图流程。

DCC 集成采用 USD 交换场景，专有材质与数据由 DCC 适配插件转换；贴图采用 Basis Universal 作为跨平台格式，渲染器按设备能力选择目标格式。这些扩展须通过资源管线的 GUID、依赖和完整版本契约接入。
