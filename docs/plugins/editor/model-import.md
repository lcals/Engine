# FBX / USD 模型资源导入

编辑器把一个 FBX 等美术源文件按“虚拟资源包”展示和组织：`.meta` 的 `[[subresources]]` 分别登记内部资源的 key、类型与独立 GUID，格式导入参数同样保存在 meta。运行时没有这个容器：Model 只保存可实例化的节点层级及 Mesh/Material 引用，蒙皮 Mesh 引用 Skeleton，Animation 独立引用 Skeleton，Material 引用 Texture。场景与业务可以单独使用其中一项；构建按选定入口的实际依赖输出，未使用的子资源不进入包，源文件仍保留。合并存储包只改变独立资源的位置，不扩大加载依赖。

格式解析属于编辑器插件，运行包只读取统一生成资源。`ENGINE_EDITOR_PLUGINS` 分别选择 `fbx_import` / `usd_import`；宿主通过 `AssetPipelineSystem::add_importer` 注册新 id。已有 Mesh 建模转换通过 `set_importer` 替换同一 id；源格式 id 与产物 AssetKind 分开，所以多个格式均可输出 Model。

| 范围 | 职责 |
| --- | --- |
| C++ 运行时核心 | Model/Mesh/Skeleton/Animation 的 FlatBuffers Schema 与单项 ResourceCodec、Scene 实例化、Animator 与 AnimationSystem；核心不读取 FBX/USD 源文件 |
| 编辑器基础 | 源扫描、meta 主/子 GUID、增量依赖、oneTBB、SQLite、独立缓存与原子目录发布 |
| 编辑器格式插件 | ufbx/OpenUSD 解析、源格式设置、子对象枚举、坐标转换与运行数据生成，各插件单独声明依赖 |
| MCP | 使用现有资源导入、打包、同步命令及模型实例化/动画控制命令；协议层不复制模型解析、骨骼采样或场景业务 |

一个 FBX/USD 源的 `.meta` 保存主 GUID 和 `[[subresources]]` 的 `key/kind/guid`。Model 保存节点层级及 Mesh/Material 引用；Skeleton、Animation 独立存在，分别通过实际使用它们的资源引用。每个资源有独立 `cache/gen/<完整GUID>`，无后缀。逻辑路径为 `models/person.fbx/meshes/<节点层级>`、`.../skeleton`、`.../animations/<动作名>`；USD 网格使用 `.../meshes/<Prim 路径>`。暂时消失的子资源保留 meta 身份以供再次出现时复用，活跃与保留 GUID 均参与冲突检查。

- FBX 插件使用 MIT 许可的 ufbx v0.23.1，固定提交 `26a482ae66871d7de36eb722aa060bce95bce274`。将坐标统一为米和右手 Y-up，保留根变换；网格包含角点拆分后的 UV、顶点色与最高四项归一化蒙皮权重，骨架包含逆绑定矩阵，动画按 meta 的 `sample_rate`（默认 30）采样。仅支持线性/刚性蒙皮，双四元数、混合形状和几何缓存明确失败。源材质和贴图尚未转换；未配置映射时使用内置 PBR，可通过 `material_remaps` 引用已有项目材质，或在实例化后覆盖材质。现有庭院示例采用脚本覆盖。
- FBX 的源切线与副切线有效且使用源法线时，保留源切线并根据副切线推导手性；源切线缺失、无效或重算法线时，对带 UV 的三角网格在编辑器离线调用 MikkTSpace（vcpkg 固定上游提交 `3e895b49d05ea07e4c2133156cfa94369e19e409`，zlib 许可）逐角生成切线。不同切线帧共享顶点时拆分顶点，并保留 UV、顶点色、蒙皮及材质分组；缺少 UV 时不生成贴图切线。其他网格来源仍使用其原有路径，不能因此声称全部网格已使用 MikkTSpace。上游版本与许可证见 [MikkTSpace 说明](../../../third_party/notices/mikktspace.md)。
- 传入 MikkTSpace 前校验位置和 UV 能以有限单精度数表示，与 GPU 网格精度约束一致；无效源切线触发完整切线流重建。转换完成全部角点与拆点后才发布候选，失败保留原完整输出，不混入部分几何；路径和骨架 GUID 沿用调用方分配的资源身份。
- 不带网格的 FBX 动画源可在 meta 用 `skeleton = "models/person.fbx/skeleton"` 引用同批导入中由另一个 FBX 自身生成的骨架；不允许转引骨架，也不能用此设置替换带网格 FBX 的骨架。空值使用本源骨架。外部动画按完整节点路径采样源局部 TRS；缺少加权骨骼或祖先时失败，缺少非必需节点时不生成其轨道，已有节点未打键也仍采样源姿态，不进行语义或参考姿态补偿。示例 `projects/showcase` 在编辑器导入可再分发的 PAB 模型和 jog/run/walk 动作；默认运行清单只选 walk，jog/run 和源静止 Take 不进入运行包。来源和许可保存在项目内。
- USD 插件独立引入 `usd-import` vcpkg feature。当前只接受自包含的静态 Xform/Scope/Mesh，网格要求 `subdivisionScheme = "none"`、三角形或凸且共面的多边形；世界变换烘焙入顶点，生成平面法线和内置 PBR。外部 layer/reference/payload（包括缺失引用）、动画、实例、细分、洞、未知几何及 Material/Shader 节点明确拒绝。此范围不代表完整 USD 场景导入。

后续格式增加自己的编辑器插件、独立 CMake 清单及 `AssetImporter::id/matches/discover/convert` 实现即可。`discover` 只描述稳定子资源 key 和类型，GUID 分配与发布仍由管线处理。尽量输出已有运行 Schema；新增运行语义时先评审 Schema/ResourceCodec 契约，再增加核心单项能力，不把源格式 SDK 或编辑器设置传入运行时。类图见 [FBX](class-diagrams/fbx-importer.md)、[USD](class-diagrams/usd-import.md)和[模型资源](../../runtime/class-diagrams/model-resources.md)。

## 模型坐标与材质映射

模型适配由导入属性组合完成：FBX `.meta` 的 `scale_factor` 控制统一缩放，`rotation_offset` 指定 XYZ 欧拉角（度），`material_remaps` 按 Mesh 子资源路径绑定各材质槽。模型、骨架和动画使用一致的导入坐标；外部动画与引用骨架的坐标设置必须相同。材质引用自动进入模型的运行依赖，参数变化参与增量构建，已有子 GUID 保持不变。错误配置保留上一批有效缓存，不能通过项目名或模型名分支修补。字段格式与限制见 [FBX 导入契约](class-diagrams/fbx-importer.md)。
