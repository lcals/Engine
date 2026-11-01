# 地形与建筑破坏：跨平台及验证范围

更新日期：2026-11-01。本文保留开源选型依据、正式实现边界和已完成验证，区分上游平台声明、独立探针与完整引擎场景。功能统一在 `projects/showcase/` 验证，当前保持单线程；本轮优化已经结束，后续工作不在本次继续实施。

地形、建筑破坏、地基/撞击联动、应力、土石、静态导航和火箭场已实现。macOS 已完成记录中的定向测试与窗口流程；Android ARM64 仅部分独立探针通过，不能推断完整 APK、实体手机或其他平台已经验收。最终性能数值统一见[最终可信基线](../../runtime/performance.md#最终可信基线)：增量地形碰撞样本满足单次 <0.5 ms，**Jolt 单步和整体物理仍未达到 0.5 ms**。

## 结论与采用范围

- 地形采用 CPU 密度数据、Transvoxel regular-cell 表与现有 bgfx 渲染；不引入整个 Godot。Voxel Tools 的六邻域连通/浮岛提取用于土石识别，LOD transition 尚未实现。
- Jolt 负责静态网格、动态凸形/compound、碰撞查询和角色运动。地形已使用分段静态碰撞，未变叶子和组树复用，渲染仍完整上传。
- Blast low-level、官方损伤 Shader、编辑器 Authoring 及 ExtStress 标量路径已接入；不引入 PhysX，不自写 Voronoi 或应力求解器。Toolkit 和整套示例未接入。
- FastNoise2 仍为候选；其当前 ARM/NEON 支持不能替代 Engine 的接入、确定性和设备验证。
- 后台准备、综合世界存档、动态残骸导航和真实空心建筑力学尚未完成。平台差异集中于依赖适配和构建配置，不写入项目玩法。

## 核对基线

| 开源上游 | 固定研究/接入版本 | 当前用途 |
| --- | --- | --- |
| [Voxel Tools](https://github.com/Zylann/godot_voxel/tree/045ff9326a32fd10ac6fdac6357b5a098aaf2093) | `045ff9326a32fd10ac6fdac6357b5a098aaf2093` | 连通域/浮岛算法参考，未引入 Godot 框架 |
| [Transvoxel](https://github.com/EricLengyel/Transvoxel/tree/51a494f03c5b024cd153b596bcc7152eb3cc93a6) | `51a494f03c5b024cd153b596bcc7152eb3cc93a6` | 正式子模块，regular-cell 查找表，MIT |
| [FastNoise2](https://github.com/Auburn/FastNoise2/tree/721400f9560831be2fc9790be9b72c44bee7d33f) | `721400f9560831be2fc9790be9b72c44bee7d33f`；候选发布 `v1.1.1` | 尚未接入 |
| [Blast](https://github.com/NVIDIAGameWorks/Blast/tree/636e7eb43b8d41e44c40eb1b4799bfd027f17ba7) | `636e7eb43b8d41e44c40eb1b4799bfd027f17ba7`，5.1.0 | 正式子模块，NVIDIA BSD-3-Clause；附属依赖保留各自许可 |
| [Jolt](https://github.com/jrouwe/JoltPhysics/tree/v5.4.0) | 5.4.0 | 已有物理依赖，MIT |

正式复用保留固定版本、源文件许可和必要来源说明；Voxel Tools 的完整 MIT 声明见[许可记录](../../../third_party/notices/voxel-tools-terrain.md)，Blast 兼容补丁及其依据位于 [third_party/patches/blast](../../../third_party/patches/blast/README.md)。商业/私有参考源码、实现和资源不得进入仓库。

## 平台支持证据

| 技术 | 上游证据 | Engine 已验证与限制 |
| --- | --- | --- |
| Voxel Tools CPU 路线 | GDExtension 构建矩阵含 Windows、macOS、Linux、Android arm64、iOS arm64 | 仅适配所需算法；上游矩阵不等于本引擎各平台功能通过 |
| Transvoxel | CPU 算法和查找表不绑定图形 API | Mac/Android ARM64 普通单元独立探针通过；正式地形插件已有 Mac 验证，其他平台正式集成未验收 |
| FastNoise2 | 官方声明桌面、Android/iOS 与 NEON 支持 | 尚未完成 Engine 接入及移动端验证 |
| Jolt | 官方列出桌面、Android/iOS 对应架构，ARM64 使用 NEON | 步骤 0 Mac/Android ARM64 验证静态 MeshShape 替换；本轮新增分段/compound 优化只完成记录中的 Mac 正式验证 |
| Blast low-level | 官方工程主要覆盖 Windows/Linux x64，头文件含 Apple/Android/ARM 条件 | 目标级链接适配后 Mac/Android ARM64 独立探针通过；正式 Mac 插件通过，Android Graph 探针不是完整 APK；Toolkit 未验收 |
| Blast ExtStress | 原默认路径包含 x86 SSE/AVX 和 CPU 探测 | 固定标量兼容补丁后 Mac 正式定向测试、Android ARM64 独立探针通过；完整移动宿主及其他平台集成未验收 |
| Blast Authoring | 官方桌面 Windows/Linux 构建路径 | Mac 编译、预破碎/导入测试和 MCP 预览通过；制作工具只进入桌面编辑器，设备消费独立资源 |

依据：[Voxel Tools 矩阵](https://github.com/Zylann/godot_voxel/blob/045ff9326a32fd10ac6fdac6357b5a098aaf2093/.github/workflows/extension_builds.yml)、[FastNoise2 平台说明](https://github.com/Auburn/FastNoise2/blob/721400f9560831be2fc9790be9b72c44bee7d33f/README.md)、[Jolt 平台说明](https://github.com/jrouwe/JoltPhysics#supported-platforms)、[Blast 构建配置](https://github.com/NVIDIAGameWorks/Blast/blob/636e7eb43b8d41e44c40eb1b4799bfd027f17ba7/premake5.lua)。iOS 第三方可行性不代表 Engine 宿主、打包和设备链已完整实现。

## 步骤 0 实测记录

2026-10-31 使用独立 C++20 Release 程序、Ninja/ccache 和已有 Jolt 5.4.0 静态库验证基础能力。Mac 环境为 M1 Max / AppleClang 21；Android 使用 NDK r27c、目标 API 28，在 Android 14 ARM64 模拟器执行。该步骤不是正式引擎场景、APK 或手机性能测试。

| 独立验证 | 已通过范围 |
| --- | --- |
| 地形普通单元 | 256 个 Transvoxel regular-cell 表项；相邻 16³ 块的平面和球形挖洞；有限值、索引、非退化三角形及接缝端点/绕序 |
| Blast 基础损伤 | 三块连接体的部分损伤、断裂、组归属与完整释放，重复 256 次；不含 Toolkit、应力或 Authoring |
| Jolt 安全替换 | 512 三角平面变为 504 三角带洞平面；候选准备时旧形状可查，SetShape 后洞口/剩余面查询正确，动态箱体正常落地 |

必要适配是目标级 `NV_C_EXPORT=extern "C"`，修正 Apple/Android 上声明与实现的链接方式；未直接修改上游检出。原 ExtStress x86 头在 ARM 上不能原样编译，后续步骤 4 已通过标量路径解决该编译问题。微型样例不代表高楼、连续爆炸或设备容量，性能结论不沿用其单次计时。

独立程序和原始回执保留在忽略的 `build/terrain-step0/` 与项目 `cache/terrain-step0/`，不作为运行资源或正式源码发布。

## 步骤 1 正式实现与验证

`runtime/terrain`、`editor/terrain` 及各自清单已正式接入普通单元表、`.terrain` 资源、组件/系统、脚本、MCP 可撤销编辑和第六个菜单入口。密度使用 `float`，16/32 格粗块，每轴最多 256 格且总样本不超过 4M；暂无逐样本材质、FastNoise2 或高度图导入。

脚本球形笔刷复用 CPU 粗块缓存，只重建脏块和梯度邻块；MCP 事务、restore、撤销/重做、Load/Reset 仍从快照或初始资源重建全部粗块。当前碰撞已升级为 4³ 叶子与 64 槽组树复用，渲染继续合并为完整 Mesh 上传。存档保存完整体积及基础 GUID/hash，不是差量块存档。

Mac Debug/Release 的 `engine.physics`、`engine.scene`、`editor.scene_persistence`、`engine.terrain_runtime`、`editor.terrain_pipeline` 及补充的 `engine.mcp_registry` 均通过。覆盖分块等价、接缝、局部修改、失败保留、空碰撞、存档身份校验、瞬态组件与增量导入；正式 Android 地形插件尚未验收。

### 步骤 1 实景记录

macOS ARM64 / Metal / Release / QuickJS 的 64×64×32 米体积验证跨块隧道、挖坑、填充、撤销/重做、保存/加载和 Reset，ETRS 哈希确认恢复一致。菜单、预览关闭及退出空场景通过；这不是大地图吞吐或其他平台场景验收。

## 适配边界

### 地形

Voxel Tools 的网格与缓存仍依赖其 VoxelBuffer、数学、分配器及 Godot 外层，不能作为独立库直接链接。其 GPU 路线使用 Godot RenderingDevice/GLSL，不能视为 Engine Metal/D3D12/Vulkan 通用实现。相关公开依据：[Transvoxel 实现](https://github.com/Zylann/godot_voxel/blob/045ff9326a32fd10ac6fdac6357b5a098aaf2093/meshers/transvoxel/transvoxel.cpp)、[GPU 与编辑限制](https://github.com/Zylann/godot_voxel/blob/045ff9326a32fd10ac6fdac6357b5a098aaf2093/doc/source/smooth_terrain.md)。

FastNoise2 的 `FASTNOISE2_STRICT_FP` 默认关闭，不能默认各 SIMD 后端由同种子生成逐字节一致结果。若以后接入运行时生成，需明确版本及确定性要求；当前资源/存档保存实际密度与基础身份。见[构建选项](https://github.com/Auburn/FastNoise2/blob/721400f9560831be2fc9790be9b72c44bee7d33f/CMakeLists.txt)。

### Blast

Blast 5.1.0 不要求 PhysX，原构建使用 repo tools/Premake；Engine 使用目标级配置选择所需模块。Authoring 和 Boost Multiprecision 留在编辑器，不另引 Cap'n Proto 或上游查看器/Exporter。Android 产物继续遵守仅 `libmain.so` 的静态合入约定，不部署 PhysX、CUDA 或制作工具。

ExtStress 已采用现有 `CGNR_SISD` / `Float_Scalar`，通过兼容补丁隔离 x86 特化、intrinsics 和 CPU 探测；不是运行时关闭 SIMD，也未引入新的求解器。支持图规模和迭代次数影响耗时，不能用渲染三角形数量直接决定承重图粒度。当前只实现现有支撑图的重力应力，不等同完整建筑材料、动态冲击应力或结构工程认证。

### Blast 移动端适配与社区证据

以下保留公开来源及其可复用边界，不将社区报告视为本仓测试结果：

- [Glavin001 标量补丁快照](https://github.com/Glavin001/blast-stress-solver/tree/230003d9a640927f1bd61d9cf4b88c84d51c1cc1/blast/source/shared/stress_solver)用于隔离 SIMD 与探测路径；关键提交为 [`a954c98f`](https://github.com/Glavin001/blast-stress-solver/commit/a954c98f56053fe79dc8cfb4f824ae08706fffde)、[`b0c090fe`](https://github.com/Glavin001/blast-stress-solver/commit/b0c090fee591396d6ca6632d4375d8f615a472ed) 和 [`230003d9`](https://github.com/Glavin001/blast-stress-solver/commit/230003d9a640927f1bd61d9cf4b88c84d51c1cc1)。SDK 修改保留 [NVIDIA BSD-3-Clause](https://github.com/Glavin001/blast-stress-solver/blob/f3955688c392661d8e9bed350542f5a5cdd39383/blast/PACKAGE-LICENSES/blast-sdk-LICENSE.md)，只提取必要兼容改动，不引入 Rust/Rapier 框架。本仓已落地补丁和范围见 [Blast 补丁说明](../../../third_party/patches/blast/README.md)。
- [DemiEngine 固定版本报告](https://github.com/JEAPI-DEV/DemiEngine/blob/b5f94e9e1cfc79482690c166b240e03e6ff2ed6e/docs/3d-compound-transition-feasibility.md)提供 Blast low-level→Jolt compound 的公开实现/Android 报告；[接入文件](https://github.com/JEAPI-DEV/DemiEngine/blob/b5f94e9e1cfc79482690c166b240e03e6ff2ed6e/cmake/DemiBlast.cmake)和项目为 [BSD-3-Clause](https://github.com/JEAPI-DEV/DemiEngine/blob/b5f94e9e1cfc79482690c166b240e03e6ff2ed6e/LICENSE)。其版本、设备和原始日志不等同本仓，不能照搬计时或推断应力/完整游戏通过。
- [PhysX issue 360 的手机演示报告](https://github.com/NVIDIA-Omniverse/PhysX/issues/360#issuecomment-2688366260)属于社区报告，未独立复现，不能作为 ExtStress、iOS 或本仓正式集成证据。
- [sse2neon](https://github.com/DLTcollab/sse2neon/tree/2272251cc3ad9cba8b8c47c4065811cfb396b802) 与 [SIMDe](https://github.com/simd-everywhere/simde/tree/65c15e6b545361f6dd89b1e3bcc76273e1344653) 均为 MIT 候选，当前未接入；前者的 SSE 覆盖不能自动解决 Blast AVX/FMA，后者自身 ARM CI 也不等于 Blast 设备验收。

## 步骤 2 当前实现与验证范围

运行时使用 low-level 与官方 `NvBlastExtFalloffGraphShader`；编辑器使用切片/Voronoi、体积积分及 `bondsFromPrefractured(EXACT)`，按实际三角面接触生成连接。输入仍是最多 256 块的参数盒体，不宣称任意 FBX、空心楼体、层级碎裂或运行时重切割。

每个连通组拥有普通瞬态 Node、MeshRenderer、Collider(compound) 与 RigidBody，要求源根节点正等比缩放。完整图/组快照支持损伤、设置和重置的撤销。步骤 5 已改为复用兼容未变组、只发布变化组；地基/撞击联动和应力由后续步骤接入，当前仍同步执行。

| 验证 | 已完成范围 |
| --- | --- |
| macOS Debug/Release | 当时九项定向 CTest 通过：physics、MCP 注册器、scene、场景持久化、destruction graph/runtime、authoring/pipeline/commands；另有源 Transform 禁用回归 |
| 导入与 MCP | 48/24/8 块示例资源 ready；损伤拆组、undo/redo、预览关闭和空场景通过 |
| 正常菜单/UI | 第七入口的柱、墙、塔损伤及实际运动、Reset、镜头切换、返回菜单和停止清理通过 |
| Android ARM64 | 独立 DestructionGraph 编译/运行通过；不是正式 Jolt/渲染宿主、APK 或手机性能测试 |
| 其他平台 | 本轮 iOS、Windows、Linux 正式插件集成未验收 |

拆组不等于所有片块立即分离，已断键块仍可通过物理接触互相支撑。项目使用显式爆心/冲量，不在核心写试件特例；该场景不能替代完整楼体承重验收。

## 步骤 3 综合联动验证

第八入口 Combined destruction 联通地形、Blast、Jolt、Effekseer 和音频。核心只提供指定 Collider 射线与非消费撞击快照，项目显式刷新支撑并组织伤害预算与表现，两个运行时插件不形成循环依赖。`estimatedImpulse` 来自官方 `EstimateCollisionResponse` 的碰前估算，不等于求解后实际冲量。

Mac Debug/Release 当时各六项定向测试通过：physics、script runtime、MCP 注册器、destruction graph/runtime 和 destruction commands。真实窗口验证挖地基下落、撞击再损伤、爆破、填土、Reset、三镜头、角色地面短走及返回/停止清理；Effect/Audio 查询确认播放请求生效，未做听音质量评估。

该步骤不构成洞穴/残骸通行专项、完整建筑承重或其他平台联动验收。综合世界存档尚未实现，模块快照、编辑器撤销和 Reset 不能替代跨模块游戏存档。

## 步骤 4 应力、土石、导航与火箭场

### 实现、来源与边界

- **ExtStress：** 使用官方标量求解器，将连接血量换算为剩余承载面积，再映射求解损伤；实例旋转和正等比缩放参与重力转换。world-bond 原点兼容补丁使用官方已有实际连接中心规则，原点平移不变性、面积及变换回归已覆盖。
- **土石：** 固定 Voxel Tools 六邻域/浮岛流程识别不连接下/侧边界的区域；Transvoxel 生成表面，逐单元凸形形成 Jolt compound。候选或预算失败不删除未转换样本；运行时上限 128，项目预算 96。继续挖填保留碎块，Reset 清理；塌方后的静态存档明确拒绝，直到 Reset 或换源。
- **导航：** 核心复用 Recast 1.6 `Sample_TileMesh` 和 Detour，调用者显式提交静态 Mesh、世界矩阵及新旧 dirty AABB。失败失效相关旧瓦片，路径查询只返回完整路径；没有自动场景扫描、动态残骸导航或代理避让。
- **火箭场：** 塔/墙/柱/梁共 520 个预破碎片段，实际刚体数由连通组决定；弹池 64 槽，请求 2/6/12 发每秒，默认 6，60 发齐射可停止，每帧至多发一枚且不追赶积压。每段实际球扫命中后才施伤、挖土、塌方和刷新支撑/导航。
- **项目预算：** 有界特效/声音节点池、每帧最多 2 次且重置间最多 24 次二次碰撞损伤；应力按 0.5 秒、导航按 0.25 秒节奏显式同步请求。这些是项目策略，不是引擎设备容量承诺。

### 已完成验证

Mac Debug/Release 当时各八项定向测试通过：script runtime、MCP 注册器、navigation、navigation script、terrain runtime、destruction graph/runtime 和 destruction commands。覆盖细支撑切断、预算失败保留、实际下落、继续挖填、静态存档拒绝、生命周期、导航瓦片边界与失败失效。

Android ARM64 模拟器的独立 Graph/ExtStress 探针通过，包括面积归一化和原点平移不变性；未覆盖正式 Engine APK、Jolt/渲染场景集成或手机性能。

### 步骤 4 实景验收

Mac ARM64 / Metal / Release 真窗口完成两档各 60 发的连射、停火、土帽坍塌、弱梁应力、Reset、角色地面短走及返回菜单。弹体按实际碰撞命中或到期，不伪造爆点；Reset 恢复原始试件和土石数量，停止后场景/节点/脚本已清理。播放状态已查验，不代表听音质量评估。

面板的 `Date.now()` 指标是墙钟帧间隔，包含限帧及采集干扰，不能代替 CPU/GPU 阶段计时；步骤 5 已改用 Tracy 固定流程建立正式基线。原始阶段回执、截图和日志保留在忽略的 `build/terrain-step4/` 与项目 `cache/`，逐轮性能数据不在本文重复维护。

### 未完成范围

综合世界存档、动态土石/建筑残骸导航、后台准备/取消/版本队列、分块 GPU 上传、完整空心楼体承重与材料模型、实体手机长期负载与发热验收均未完成。分段碰撞增量提交已在步骤 5 实现，不再列为未完成能力。

## 步骤 5 实测基线与可复用流程

可选 Tracy 已覆盖地形、破坏、物理、导航等 CPU 阶段，提供完整帧标记及 bgfx GPU/资源数据；采集、窗口分析与碰撞预算工具位于 `scripts/performance/`。完整原始证据保留在忽略的 `build/`，方法和最终数据统一见[最终可信基线](../../runtime/performance.md#最终可信基线)。

当前已实现 Scene 索引/子树删除、未变破坏组复用、物理共享快照和速度原地更新、根动态体原始姿态保留、官方临时分配器复用、地形 4³ 叶与 64 槽组树、局部唤醒及塌方标脏。严格完整盒形可在成功构建 Hull 后使用保留实际参数的官方 BoxShape；缺角、不规则和微斜顶维持 Hull。完整说明见[当前实现](../../runtime/performance.md#当前实现)。

最终 Mac Debug（Tracy OFF）和 Release（ON）受影响构建及五项定向 CTest 通过，覆盖 physics、character controller、terrain runtime、destruction runtime 和 destruction commands。最新实景仅证明记录窗口中的增量碰撞预算；首次建图/Reset 未由该窗口覆盖，Jolt 单步和整体物理仍高于 0.5 ms，其他平台性能未验收。

## 性能判断及当前接入缺口

以下均为剩余工作，不在本轮继续修改源码：

| 环节 | 当前事实与后续边界 |
| --- | --- |
| 碰撞与物理 | 变化叶和组树已复用，原碰撞精度/迭代数保留；窄相、接触和同帧固定步累计仍未达预算，需进一步区分成本 |
| 全量地形 | 首次建图、Reset 和恢复仍有同步全量成本；同帧重复提交合并未实现，须保留即时查询与失败原子性 |
| 塌方与连通扫描 | 已缩小碰撞扫描和标脏范围，但连通识别/事务仍同步；后台化需独占快照、版本和取消，不直接搬迁场景操作 |
| 渲染上传 | `MeshRenderer::set_mesh_data` 仍完整复制 Mesh，revision 后重建 GPU 几何及切线，尚无分块 GPU 更新 |
| 导航与表现 | 静态导航可局部重建，动态残骸和代理未实现；其他模块热点只记录，另行指定范围后再优化 |
| 线程与内存 | 当前单线程；未来异步必须限制在途快照、过期结果及发布预算，并观察内存和长期负载 |

总刚体/角色上限 4096 是配置约束，不能写成已实测支持 4096 活跃碎块。三角 Mesh 继续仅用于静态碰撞，动态组使用凸形/compound。官方接触缓存仍须在根形状变化时正确失效，不能以叶子共享绕过该契约。

上游讨论可参考 [Voxel Tools 性能说明](https://github.com/Zylann/godot_voxel/blob/045ff9326a32fd10ac6fdac6357b5a098aaf2093/doc/source/performance.md)与 [Jolt 架构](https://jrouwe.github.io/JoltPhysics/)，不能把其环境中的倍数或容量当作 Engine 结果。

### 内存边界

当前每点为 32 bit 浮点密度，样本包含块边界；采样间距减半会使三维样本量约增至八倍。运行峰值还需计入网格、碰撞树、共享快照、GPU 与碎块，不能只用密度数组容量估计整场内存。RSS 是进程总量，不是地形或物理单项占用。

## 首个项目内的分步实现与测量

| 步骤 | 当前完成范围 | 仍需区分的验收边界 |
| --- | --- | --- |
| 0. 技术验证 | Mac/Android ARM64 的基础算法与 Jolt 独立探针 | 不是完整引擎或手机性能 |
| 1. 地形实验场 | 跨块挖填、颜色、保存恢复与 Mac 窗口流程 | LOD、分块上传、正式移动插件未验收 |
| 2. 建筑破坏场 | 官方预破碎、组拆分/运动、撤销和 Mac 菜单流程 | 参数实心试件，不是任意模型或空心楼体 |
| 3. 综合联动场 | 挖地基、撞击再损伤、表现和清理 | 综合存档、洞穴/残骸专项未完成 |
| 4. 同步玩法补齐 | 应力、动态土石、静态导航、Mac 火箭场与 Android 独立应力探针 | 正式移动场景和手机长期负载未验收 |
| 5. 单线程性能优化 | 分段/组树及物理准备优化，最终 Mac 基线已记录 | Jolt 单步和整体物理未达 0.5 ms；本轮停止优化 |

后续测量固定资源版本、镜头和操作序列，使用 Release 采样、Debug 检查正确性；分别报告单次、同帧累计、完整帧和固定步，保留零步/追赶帧及首次全量成本。平台只记录实际验证范围，模拟器不替代实体手机。测试与采样按当前模块选择，不触发全仓依赖重编或自动 CI。
