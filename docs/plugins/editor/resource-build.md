# 资源构建与运行清单

[编辑器资源管线](../../editor/asset-pipeline.md) · [资源构建类图](class-diagrams/asset-build-system.md) · [运行时资源加载](../../runtime/resource-loading.md)

编辑器基础 `AssetPipelineSystem` 负责扫描与增量导入，编辑器插件 `AssetBuildSystem` 负责根据项目清单打包运行资源。两者分别维护导入缓存与运行输出，复用运行时核心的目录、实体编码和读取校验接口。

## 编辑器与真机

编辑器扫描全部 `assets/` 和邻接 `.meta`，形成完整资源目录；首次导入全部资源，之后只转换失效资源，产物仍为 `cache/gen/<完整GUID>`。编辑器运行项目无需先打包。扫描、重新导入及清单更新均由编辑器发起；成功后由编辑器协调调用核心 `ResourceSystem.update_catalog()`，核心只接收准备好的目录数据和存储映射。

真机使用构建清单选出的资源集合。启动时读取运行清单并更新核心目录一次，正常运行期间集合固定。读取清单只建立 GUID、逻辑路径、运行依赖和存储定位索引，不读取全部资源实体；具体资源在使用时读取。

核心由 `ResourceSystem`、`ResourceCatalog`、`VirtualFileSystem` 和 `Resource` 承担目录、定位与单项读取职责，`ResourceLoader` 声明加载契约，运行时资源依赖图插件实现依赖加载与共享缓存；具体契约见[运行时资源加载](../../runtime/resource-loading.md)。核心不扫描源目录、不监听文件变化、不自动检查版本，也不区分编辑器模式。编辑器根据自身业务调用核心，运行时不依赖编辑器模块。

这一边界来自全引擎的[调用方驱动核心](../../architecture.md#调用方驱动核心)原则，不仅用于目录更新。编辑器使用核心提供的最小能力组合导入、运行和构建业务；玩法业务同样通过核心完成自己的流程，不需要核心识别调用者。

## 职责

| 模块 | 负责内容 |
| --- | --- |
| `AssetPipelineSystem` | 扫描资源和 meta、校验身份、记录构建依赖与运行依赖、管理 SQLite 版本、调度增量转换。 |
| `TaskSystem` | 使用 oneTBB 执行转换任务；由资源管线读取 `settings/editor-import.toml` 并配置，项目显式配置导入并发 8。 |
| `AssetBuildSystem` | 借用资源管线，读取打包清单、计算运行资源闭包、组织输出、复用构建结果、校验并发布运行清单。 |
| `ResourceSystem` | 接收目录与存储映射，提供 GUID / 路径转换和资源读取；不负责扫描、导入或打包。 |
| MCP | 调用编辑器构建接口并返回结果，不另写依赖分析、转换或打包算法。 |

`AssetBuildSystem` 继承 `System`，由编辑器宿主管理生命周期。它不替代导入管线，也不直接激活正在运行的项目资源；活动清单何时切换由编辑器协调。

## 项目打包清单

项目只维护一份可选的 `settings/editor-resource-build.toml`，各平台共用。它描述项目实际使用的资源入口，不需要填写安装目录、APK 路径或引擎内置资源清单；平台由构建调用选择，导出工具负责部署到统一内置资源根。

```toml
[[common.items]]
name = "game"
storage = "package"
roots = ["scripts/game.ts", "scenes/main.scene"]
keep = []
```

- 清单文件不存在、没有 `[common].items` 或 `items = []` 时，构建全部项目目录资源；默认以一个 `game` 合并包输出。这里的全量指经过导入的资源实体，不是复制项目的 `assets/`、`settings/` 和 `cache/` 源目录。
- 显式 `[[common.items]]` 必须写 `roots` 数组。`roots = []` 表示有意不选择该项的项目入口；仍可用 `keep` 选择动态资源。空项目集合不会移除引擎必需的内置资源基线。
- 普通资源逻辑路径相对项目资源根，例如 `scenes/main.scene`；不带 `project/`。`builtin://` 是引擎保留域，`editor://` 仅属于编辑器，不能作为运行构建入口。
- `roots` 是入口；`keep` 补充无法静态发现的动态加载资源。两者均递归包含真实运行依赖，不需手写每张间接贴图、每个材质或 Shader。
- `storage = "files"` 保持 `gen/<完整GUID>` 散文件；`storage = "package"` 按 GUID 排序串联完整 `ERS1` 实体，输出为 `packs/<构建项名>-<指纹>`，无后缀。catalog 记录实体的包内偏移和长度，不增加包内目录。

构建项名称只使用小写字母、数字、连字符或下划线；`builtin` 保留给引擎。未知字段、重复名称、错误类型、越界路径和项目子路径中的符号链接均拒绝。清单不接受 `target_platform`；平台只由调用参数指定，支持 `macos/ios/windows/android/linux`，省略时采用宿主平台。默认输出 `cache/build/<platform>`；高级配置可在 `[common]` 下选填 `output = "cache/build/<专用目录>"`，不能覆盖导入缓存或内置 Shader 工具缓存。

一次 `build()` 可以选择一个或多个构建项；未指定名称时选择清单中的全部项。无已配置构建项时不能按名称选择。未知项、重复选择、缺失入口和不兼容的存储配置返回失败。所选项共同生成一份 `catalog.bin`，同一 GUID 只出现一次。同种存储方式的共享项目依赖归名称排序最前的选中项；同一项目资源同时要求散文件与合并包时明确报错。

业务只使用逻辑资源名；Windows 的程序旁 `assets/` 和 Android 的 APK `assets/` 都是宿主映射的内置内容根。`gen/`、`packs/` 和平台部署位置由构建与导出工具维护，不进入业务引用。打包不携带 SQLite、源 meta、项目设置或仅编辑器资源。

## 两种依赖

**构建依赖**用于判断转换结果是否失效，例如源文件、Shader include、TypeScript 声明、Schema、编译工具与转换设置。它们变化时触发相关产物重建，但不因此自动进入运行包。

**运行依赖**用于决定运行时必须携带什么，例如场景引用 Mesh 和材质、材质引用 Shader、脚本模块引用其他运行模块。构建项从入口和保留项出发，沿运行依赖递归计算闭包；每个 GUID 只处理一次。缺失依赖或目标平台不匹配时构建失败。

`builtin://` 固定 GUID 资源同样参与依赖解析与校验。每次运行构建自动携带当前 11 项引擎基线（默认 Shader、材质、立方体和占位纹理），写为独立 `packs/builtin-<指纹>`，用户不维护其名单。身份和依赖来自 `builtin_resource_catalog()`；载荷由与启动备用定义共用的 `builtin_resource_payload()` 构造。它们不生成项目源文件或 meta，也不允许项目占用固定 GUID 或保留域。

内置 Shader 使用随编辑器部署的 shaderc、bgfx include 和引擎 Shader 源码，按目标平台重新编译：Windows 为 `s_5_0`、macOS/iOS 为 Metal、Android/Linux 为 SPIR-V。不能把宿主编入的 DirectX 字节码复制到 Android。工具和源数据指纹、输出摘要校验后的编译缓存位于 `cache/build/.builtin-shaders/`，未变化时复用；正式运行实体仍只进入统一 catalog 与内置包。

无项目或未提交外置内置条目时，引擎保留编入程序的启动备用定义；运行包中已有定位的内置条目必须从所选资源根读取、校验并解码，缺失或损坏不能回退到编入定义。项目资源可以引用引擎内置资源，内置定义不能反向依赖项目资源。

两种依赖分别记录在 SQLite 的 `dependencies` 与 `runtime_dependencies` 表中。Shader 源码和 include 只属于构建依赖；TS 根据实际生成的 JS import / export 和字面量动态 import 记录运行模块依赖，类型导入不进入运行图。非字面量动态路径仍需显式入口或保留项。普通图片、文本等资源仍在完整编辑器目录中，可通过 `roots` / `keep` 选入运行包。

## 增量构建与发布

运行中的编辑器项目必须先由调用方执行停止操作；导入和构建不会自动停止或重启项目。资源管线在项目锁内探测 `cache/editor-hosts/` 中的会话 OS 租约；活宿主通常要求匹配会话的 idle/closed 状态。本进程编辑器协调线程还可在 preview 下同步增量导入，但必须同时验证 running=false、App 已初始化、持久目录一致且 App 内置资源目录对应本项目 cache；外部工具不能借其他宿主的 preview 状态覆盖缓存。启动中、运行中、停止中、状态缺失或损坏均拒绝。心跳过期不影响保护，进程退出自动释放租约，残留登记不阻塞导入。调用方暂停使用相关资源、导入并发布后，直接调用 `update_catalog(data, mounts)`；运行项目的重新部署仍须先停止，清单更新失败保持停止。完整保护契约见[资源管线类图](../../editor/class-diagrams/asset-pipeline-system.md)。

1. 资源管线扫描项目，并为目标平台完成必要的增量导入。继续复用 SQLite 记录和未变化的 `cache/gen/<完整GUID>`。
2. 构建系统解析所选构建项，计算运行依赖闭包并确定存储布局。全量编辑器目录与精简运行清单分别生成，不能用精简清单缩减编辑器可见的资源。
3. 根据目标平台、输出配置、包格式版本及排序后的资源 GUID / 内容摘要判断输出是否可复用。独立文件只更新变化项；合并包内任一资源或布局变化时重建整个包，未变化的包直接复用。
4. 在暂存输出中校验完整清单、依赖、资源摘要和包内定位，确认每个资源都能解析到对应数据。读取清单的运行时路径无需因此读取全部实体；完整输出校验属于编辑器构建职责。
5. 输出完整成功后发布运行清单，不能先发布清单再补齐资源。涉及活动资源时按上述释放、发布、直接更新目录的顺序交由编辑器协调。

发布后 `last_build()` 记录本次成功结果与输出根目录；失败通过 `false` 和 `last_error()` 返回，并保留上一份成功统计。输出先在同级暂存目录完整校验，再交换正式目录；失败保留上一份运行输出，进程中断后可恢复备份。已成功提交的导入缓存可保留供下一次复用，打包失败不回滚这笔独立导入事务。

`cache/gen/` 只有一份当前目标的产物，不增加平台子目录。异平台构建后不能把该目录提交给宿主运行时；恢复编辑器项目时先重新导入宿主目标，再更新核心目录并启动。同一份配置由平台调用生成各自 `cache/build/<platform>` 发布目录，仍不构成第二套导入缓存；高级自定义输出由调用方避免跨平台覆盖。

## 工具入口

`build_resources(manifest?, items?, targetPlatform?)` 使用当前项目；省略 `manifest` 时读取固定 `settings/editor-resource-build.toml`，缺失则全量。可选 `targetPlatform` 决定平台，省略时采用宿主；`items` 为可选构建项名称。MCP 调用 `AssetBuildSystem.build(project_root, manifest, items, target_platform)`，返回成功状态、构建统计或错误说明，不另建配置或算法。

命令行使用 `asset_build_app --project <项目根目录> --package [--manifest settings/editor-resource-build.toml] [--item game] [--target android]`；`--manifest` 本身也选择打包模式。成功时标准输出为单行 JSON，包含 `output`、`targetPlatform`、`resourceCount`、`packageCount`、`reusedPackageCount` 和 `revision`，资源和包统计包含自动内置基线；错误写入标准错误。未指定 `--package` 和 `--manifest` 时执行独立的纯导入操作，`--target` 同样适用。原生构建见[构建指南](../../building.md)，资源命令见[运行包命令](#运行包命令)。

真机同步消费这份运行清单及其引用的独立文件和包，不在同步工具中重新计算资源集合。

## 运行包命令

```sh
./build/ninja-engine-release/asset_build_app --project projects/showcase --package
./build/ninja-engine-release/asset_build_app --project projects/showcase --package --target android
./build/ninja-engine-release/asset_build_app --project projects/showcase --manifest settings/editor-resource-build.toml
# 可重复 --item；省略时选择清单的全部构建项。
./build/ninja-engine-release/asset_build_app --project projects/showcase --manifest settings/editor-resource-build.toml --item game
```

MCP 提供 `build_resources`，接受可选 `manifest`、`items` 和 `targetPlatform`；省略 manifest 使用上述统一项目配置。`sync_assets` 可传 `source`（例如 `cache/build/windows`）同步已构建目录；省略时先导入并同步完整编辑器目录。同步成功只表示候选部署可用，是否激活由运行宿主与热重载开关决定。

命令行客户端使用同一工具协议：

```sh
python3 scripts/mcp_client.py --server build/ninja-engine-release/mcp_app --project projects/showcase --runtime-dir build/minimal-runtime build --manifest settings/editor-resource-build.toml --item game
python3 scripts/mcp_client.py --server build/ninja-engine-release/mcp_app --project projects/showcase --runtime-dir build/minimal-runtime sync --source cache/build/windows
```

编辑器直接使用 `cache/gen`，不要求打包。会覆盖该目录的导入或构建要求先 `stop_project`；运行中会返回明确错误。同步已有的独立运行包不覆盖该缓存，可以在关闭热重载时准备候选版本。`cache/gen` 同时只保存一个目标平台的产物，构建 Android 后要先重新导入宿主平台产物，再启动桌面项目。
