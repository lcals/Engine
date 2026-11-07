# 资源管线

[资源管线类图](class-diagrams/asset-pipeline-system.md) · [项目与出包](project.md) · [跨模块数据](../runtime/data.md)

导入收集完整 `assets/` 信息并生成增量缓存；[资源构建系统](../plugins/editor/resource-build.md)根据打包清单选择运行子集。构建依赖与运行依赖分开维护，编辑器直接把成功导入的目录提交给核心，无需先打包才能运行项目。

## 源资源与身份

项目源资源位于 `assets/`。场景、预制体、网格、材质和 Shader 描述分别使用 `.scene`、`.prefab`、`.mesh`、`.material`、`.shader` 后缀，内容为 TOML；不追加 `.toml`。TypeScript、Shader 源码、图片及其他文件保留其格式。

美术默认采用“源模型 → 预制体 → 场景实例”流程，也可直接将引擎节点子树保存成预制体。模型源保留用于再导入，场景保存实例关联与明确属性覆盖；普通保存不回写模板。制作、覆盖与 MCP 命令见[预制体工作流](prefabs.md)。

每个源文件都必须有邻接 `.meta`，以 TOML 保存唯一完整 GUID。新资源自动生成 meta；移动资源时一起移动 meta，保持身份；复制资源不能复用原 GUID。重复 GUID、已知资源丢失 meta 均返回失败，不静默重写身份。

资源导入修复由源文件的 `.meta` 配置驱动，例如模型的法线重算、骨架引用、动画采样和材质映射，或纹理的颜色空间与压缩。具体导入器负责参数校验及转换，基础管线负责指纹、依赖、增量和原子发布。修复保留美术源文件与已有子资源 GUID；参数错误或转换失败不覆盖上一批有效缓存。运行时只消费修复后的统一资源，不读取 `.meta`，也不按项目、模型名称或购买来源选择兼容路径。玩法中的动作状态选择和切换条件仍归项目脚本。

上述通用性要求适用于引擎及导入插件。项目自己的离线转换脚本可按项目约定筛选场景对象、转换来源数据；这些规则由项目维护，不进入引擎，也不要求为此重写已正确工作的项目转换流程。

核心模块的 `GuidMap` 提供逻辑路径与 GUID 的双向映射，编辑器、桌面和设备运行时共用。它不依赖 SQLite 或平台路径；普通逻辑路径直接相对 `assets/`，引擎资源使用 `builtin://`。

## 转换与发布

`AssetPipelineSystem` 扫描源文件、校验 meta、建立依赖图，通过 `TaskSystem` 下发转换任务。任务系统使用 oneTBB，只负责依赖调度；`settings/editor-import.toml` 必须显式提供 `[common].import_threads`，项目配置为 8，允许 1–64。源扫描、版本决策、文件发布和错误报告归资源管线。

系统属于 `engine::editor`，逐类型导入接口与实现属于 `engine::editor::asset_pipeline`。每种资源在独立导入器文件中实现识别、默认 meta、指纹、依赖与转换；管线只通过登记接口调度。初始化登记默认实现，宿主可在空闲时通过 `add_importer` 登记新 id，或通过 `set_importer` 替换已有 id 的实现，关闭后清空。编辑器基础不引用或链接具体插件。

转换结果写入 `cache/gen/<完整GUID>`：文件名无扩展名、类型前缀或版本子目录。每个文件是 `ERS1` 资源实体，包含带 `ERSH` 标识的 FlatBuffers 实体头与载荷。`cache/catalog.bin` 使用版本 2，保存 GUID、逻辑路径、类型、必要运行依赖、完整实体大小和哈希、头长度、存储定位及目标平台；导入缓存的定位为 `content` 挂载下的 `gen/<GUID>`。

`cache/assets.sqlite` 使用版本 2，记录源、工具、配置和生成指纹；`dependencies` 表用于构建失效和任务排序，`runtime_dependencies` 表用于打包闭包。数据库与目录索引位于 cache 根，不进入 gen。实体哈希用于校验和增量判断，不改变 GUID 身份。

场景、网格、材质和 Shader 描述转换为各自的 FlatBuffers 载荷；TypeScript 编译为 JavaScript，Shader 源码由宿主 shaderc 编译为目标平台程序。PNG/JPG/JPEG/TGA/QOI/HDR/EXR 通过 [TextureImporter](class-diagrams/texture-importer.md) 生成 basis 或 native 纹理载荷，格式、压缩、颜色空间和 mip 设置取自相邻 `.meta`。没有专用导入器的文件保留原始字节作为载荷。转换结束后统一调用核心实体编码接口封装，不能把 `gen/<GUID>` 直接当作裸 JS 或裸 FlatBuffer 读取。

建模插件的 [ModelingMeshImporter](../plugins/editor/class-diagrams/modeling-mesh-importer.md) 由编辑器、构建 CLI 和 MCP 宿主显式注入。它从 `.mesh` 的 `[modeling]` 权威数据编译普通运行 Mesh；基础 MeshImporter 仍处理普通网格。未注入插件时，权威源导入明确失败，不使用冗余渲染数组。导入器身份和 Schema 参与增量指纹，移除插件不能绕过检查复用旧缓存。

首次导入转换全部资源，重复导入只转换变化及其受影响的依赖。TypeScript 指纹覆盖编译器实际读取的配置、声明、标准库和编译器文件；实际生成 JS 中的模块 import / export 和字面量动态 import 形成运行依赖，类型导入被剔除。Shader 的构建依赖保守包含项目全部普通源文件以覆盖自定义 include，这些源码不会自动成为 Shader 的运行依赖。

损坏产物重新生成，缓存命中也校验完整实体头、大小和哈希。删除 SQLite 后可由源文件和 meta 重建，GUID 保持不变。普通源资源仍有 meta、实体和完整编辑器目录条目，可通过打包入口或保留项显式选入运行集合。

导入使用项目锁、暂存输出及回滚记录。转换结束后调用核心 `ResourceCatalog.assign()` 校验完整目录；缺失运行依赖、运行依赖环、非法实体元数据等都在发布前失败。全部成功后发布目录，失败保留上一版；无变化时不改写正式文件或版本。

`AssetPipelineSystem.catalog()` 返回最近成功导入的完整内存目录，只包含元数据；下一次成功导入更新内容，失败保留旧目录，关闭时清空。编辑器协调层使用该数据及 `content` 挂载直接调用核心 `update_catalog()`。运行中的编辑器项目会阻止覆盖同一 `cache/gen/`；调用方必须先停止，导入工具不自动停启。管线也提供 `asset_build_app` 离线入口，命令见[离线导入](#离线导入)。

## 格式导入与子资源

[FBX](../plugins/editor/class-diagrams/fbx-importer.md)与[USD](../plugins/editor/class-diagrams/usd-import.md)是统一 [editor/asset_import](../plugins/editor/asset-import.md) 插件中的独立类型实现。`AssetImporter::id()` 描述源格式，`kind()` 描述主产物；多个格式可以输出 `model`。插件 `discover` 返回稳定子资源 key/kind，管线在源 meta 的 `[[subresources]]` 分配并保留 GUID。主产物及每个子产物各有目录记录、构建键和独立 `gen/GUID`，主 Model 用 GUID 组织节点和依赖。

一个美术源文件在编辑器组织为虚拟资源包，`.meta` 的 `[[subresources]]` 维护子项 key、类型和独立 GUID；格式导入参数同样由该 meta 管理。这个分组不生成运行时包对象。Model 只包含节点层级与 Mesh/Material 引用，蒙皮 Mesh 和 Animation 分别引用需要的 Skeleton，Material 引用 Texture。运行时各项均为独立资源；使用一个模型或动作不引用同源其他动画。构建按选定入口的实际依赖输出，无用子项不进入运行包，不删除美术源文件。散文件或合并包只改变存储方式。

已消失的子资源不会进入本批运行目录，但身份保留在源 meta 并参与全项目冲突检查。源文件、meta、Schema 或导入器指纹变化按依赖传播；没有变化时整批复用，不改写正式版本。发现阶段已知子资源丢失身份时失败，防止引用被悄悄重分配。

FBX 生成层级、几何、骨架和采样动画；Skeleton / Animation 格式 2 含离线 Ozz 归档，磁盘载荷缺失归档时拒绝读取，运行时不调用 Builder 补齐。显式 structured 内存候选仍支持动态生成。USD 当前只生成自包含的静态凸多边形模型。具体格式限制由各插件校验。核心只定义运行 Schema、ResourceCodec 和场景实例化，格式依赖不进入运行包；MCP 调用现有导入/构建/同步能力，无需复制每种格式的算法。

## 运行资源与同步

运行时读取 catalog 元数据，并在使用时读取所需资源，不在启动时把所有实体读入内存；可从独立 GUID 文件或合并包中的区间读取。运行时不依赖 TOML、SQLite、源 meta 或编辑器。Android 出包使用目标平台 catalog，拒绝把桌面 Shader 缓存混入 APK。

`AssetSyncSystem` 和 MCP 按完整运行清单同步场景、网格、材质、Shader、脚本及普通资源，以及它们引用的独立文件或包。校验后发布对应版本；纯资源目录无需玩法脚本也能同步。版本边界由调用方提供的目录和存储挂载确定，核心不扫描目录或自动切换版本。真机启动提交一次目录，正常运行使用固定集合。

热重载由 GameplaySession 协调脚本停止、资源释放、目录提交和重新运行，MCP 只负责同步和开关。材质或 Shader 改动通过完整版本重载重建场景及 GPU 资源，不是原地替换任意活跃对象。候选编译或静态链接失败保留旧实例；新入口执行后的副作用不自动回滚。

## 尚未实现的能力

由 AI 执行的角色准备、自动绑骨/蒙皮、公共动作整理和显式骨架适配配置生成见[角色与动画制作流程](character-production.md)。这些属于离线制作与编辑器扩展规划，复用本管线的主/子 GUID、指纹、依赖与失败不发布契约；当前没有接入云端绑骨服务或本地推理工具。FBX 的外部 `skeleton` 引用不是跨骨架语义重定向；引擎运行时重定向是独立目标，不能以逐角色离线烘焙代替。

编辑器文件监听与增量预览刷新见[编辑器](editor.md#编辑器场景预览)；完整 USD 场景语义尚未接入。静态 Mesh 的离线 LOD 已通过 meshoptimizer 接入，源 `.meta` 参数、适用范围与运行组件开关见[静态模型 LOD](static-model-lod.md)。OpenUSD 位于统一资源导入插件的 USD 实现；Basis Universal 的编辑器压缩与运行时转码已接入贴图流程。

DCC 集成采用 USD 交换场景，专有材质与数据由 DCC 适配插件转换；贴图采用 Basis Universal 作为跨平台格式，渲染器按设备能力选择目标格式。这些扩展须通过资源管线的 GUID、依赖和完整版本契约接入。

## 离线导入

```sh
./build/ninja-engine-release/asset_build_app --project projects/showcase
```

模型格式与配置见[FBX / USD 导入](../plugins/editor/model-import.md)，材质参数与 Shader 绑定见[渲染模块](../runtime/rendering.md)。

## 图片与半浮点纹理

普通图片支持 PNG/JPG/JPEG/TGA/QOI，线性 HDR 图片支持 Radiance HDR 与单部件 EXR。新图片的 `.meta` 自动生成贴图导入设置，示例：

```toml
guid = "<完整 GUID>"
importer = "texture"
meta_version = 1

[texture]
storage = "basis"
format = "auto"
compression = "uastc"
compression_level = 2
mipmaps = true
```

- `storage`：默认 `basis`，运行时按设备能力转码；显式 `native` 在编辑器提前转为目标平台数据，运行时只分块上传。native 的 `auto` 为 Windows/Linux BC7、macOS/iOS ASTC4x4、Android ETC2，HDR 为 RGBA16F；设备不支持时返回错误，不附带后备载荷。
- `format`：basis LDR 的 `auto` 按 GPU 与转码器能力选择，或明确 `rgba8` / `bc7` / `astc4x4` / `etc2`；HDR 只允许 `auto` / `rgba16f`，运行时保持 RGBA16F。显式格式不支持时返回失败。
- `compression`：默认 UASTC；LDR 可选 ETC1S。UASTC LDR 可显式使用 `rdo_quality_scalar`（0.001..50，默认1，越小失真越少），ETC1S 使用 `quality`（1..255，默认128）。HDR 使用 UASTC HDR 4×4，不接受 LDR RDO 或 ETC1S 选项。
- `compression_level`：UASTC LDR/HDR 0..4，ETC1S 0..6；`srgb` 省略时 LDR 默认 true、HDR 默认 false。法线、ORM 等 LDR 数据贴图显式设为 false；HDR 不允许 true。`mipmaps` 决定是否生成完整 mip 链。
- 设置、源图片、Schema 和编码器版本均参与增量键。输出为 `cache/gen/<完整GUID>` 中的 TextureData 格式 2 资源（basis 或 native）；Shader sampler 默认纹理和材质纹理引用进入统一依赖树、打包和同步。
- 旧图片若使用 `importer = "source"`，修改为 `texture` 时保留原 GUID；不自动改变已有原始二进制的用途。单边最大8192、总像素最多16M，解码前检查图片头。

**法线和 ORM 的颜色空间必须检查**：普通 LDR 图片没有显式指定 `srgb` 时按颜色贴图处理，默认启用 sRGB 解码；导入器不会根据文件名或材质绑定自动推断用途。给法线或 ORM 图片的邻接 `.meta` 增加以下设置，并保留该文件原有 GUID 和其他选项；若已有 `[texture]` 表，只需在该表中加入或修改 `srgb`，不要重复创建同名表：

```toml
[texture]
srgb = false
```

例如未压缩的 8 位通道 `128`，作为线性数据时约为 `0.502`，但误作 sRGB 解码后约为 `0.216`。对法线 XY，Shader 将其映射到 `[-1,1]` 后分别约为 `0.004` 和 `-0.568`，会明显改变方向；ORM 的遮蔽、粗糙度或金属度也会被错误改变。纹理绑定本身当前不校验颜色用途；修改 `.meta` 后重新导入，由正常增量管线更新资源。

BasisU 使用已有依赖的 C++ 编码接口，整体并行度由项目 `[common].import_threads` 显式控制（当前配置为 8），每任务编码器使用单线程。`third_party/overlay-ports/basisu/quiet-library-output.patch` 将上游 HDR 输入诊断写到 stderr，确保 MCP stdout 只承载协议。

KTX 使用独立 `texture_ktx` 导入器，支持线性 RGBA16F / RG16F 的 KTX1 二维或 cube 纹理，单层 mip 或完整 mip 链；不重新压缩或生成 mip。`.meta` 使用 `compression = "none"`、`srgb = false`，`format` 为 `auto` 或与容器一致的半浮点格式；若指定 `mipmaps`，须与源文件一致。生成的 TextureData 保留 KTX 数据，经统一目录加载后由 bgfx 创建纹理。当前不支持 KTX2、纹理数组或体纹理。

HDR 导入保持大于 1 的线性辐亮度，并在渲染和 IBL 输入中转码为半浮点；UI 只接受 LDR。BasisU HDR 当前不保留 alpha、负 RGB 或超过 65216 的辐亮度，导入会明确失败；这些数据继续使用已有线性半浮点 KTX 管线。预过滤 IBL 的 cube/粗糙度 mip、RG16F BRDF LUT 和烘焙 lightmap 保留原 KTX，不进行有损重压缩。模型外部贴图与特效纹理继续引用这套通用贴图 GUID 资源，不额外建立导入链。BMP/DDS 等没有对应导入器的文件仍是原始 source 资源，不能视作已可渲染的纹理。
