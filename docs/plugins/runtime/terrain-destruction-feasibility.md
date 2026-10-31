# 地形与建筑破坏：跨平台及性能研究

日期：2026-10-31。范围：官方源码、构建配置、步骤 0 的独立 ARM64 编译/运行验证，以及步骤 1 地形与步骤 2 建筑破坏正式插件的分阶段验证。结果区分上游声明、最小程序实测与正式引擎验证，不将独立探针或上游平台列表等同于全平台完成。

用户要求分步实现，并把全部功能测试放在第一个示例项目 `projects/showcase/`，允许自建场景和生成模型。步骤 1 同步地形插件和实验场已完成既定 Mac 验收；步骤 2 的建筑破坏插件及第七个实验场代码已接入，macOS Debug/Release 定向测试、MCP 窗口预览及最终正常菜单/UI 玩法流程已通过，按[总体方案](terrain-destruction-plan.md)继续推进。

实施顺序补充（2026-10-31）：用户要求先同步实现功能，后面再移到其他线程异步处理。本文的后台准备、队列和预算分析作为后续优化依据，不是首版门槛；同步阶段先分开计算与安全提交，记录基线耗时。

## 结论与采用范围

- 地形采用 CPU 体积数据处理、Transvoxel 网格与现有 bgfx 渲染，作为各平台共用路径。优先适配 Voxel Tools 相关算法，不引入整个 Godot，也不把它的 GPU 管线作为首版条件。
- 新版 FastNoise2 有 ARM/NEON 支持，可作生成器候选；不能套用旧版本不支持 ARM 的结论。
- Jolt 继续负责物理。步骤 1 已补通用瞬时网格碰撞提交，候选形状准备完成后安全替换；步骤 2 已增加官方凸包/compound 动态组接入，批量预算和后台调度仍需后续补齐，不能把这些工作归给 Blast。
- Blast 已按模块接入：low-level 与官方损伤 Shader 提供连接损伤和结构拆分；ExtStress 上游默认构建存在明确 x86 依赖，社区已有保留原求解器的标量适配，优先复用这些补丁，不能直接宣布本引擎 ARM 应力已可用。离线 Authoring 只进入桌面编辑器，Mac 编译、定向测试与 MCP 预览流程已通过，验证明细和剩余范围见下文步骤 2。
- Windows、macOS、Linux、Android、iOS 是技术评估范围；iOS 的第三方可行性不代表 Engine 宿主及打包链已经完整实现。平台差异集中在依赖适配与构建配置，不进入项目玩法。

## 核对基线

以下为固定研究基线。Transvoxel 和 Blast 已以表中版本登记为正式插件子模块；Blast 仅启用 low-level、ExtShaders 和编辑器 Authoring，不代表 Toolkit、ExtStress 或整套示例已接入。Voxel Tools 和 FastNoise2 仍是研究/后续候选。

| 上游 | 基线 |
| --- | --- |
| Voxel Tools | `045ff9326a32fd10ac6fdac6357b5a098aaf2093` |
| Transvoxel 查找表 | `51a494f03c5b024cd153b596bcc7152eb3cc93a6` |
| FastNoise2 | 主线 `721400f9560831be2fc9790be9b72c44bee7d33f`；发布候选 `v1.1.1` |
| Blast 独立仓库 | `636e7eb43b8d41e44c40eb1b4799bfd027f17ba7`；变更记录为 `5.1.0` |

## 平台支持证据

| 技术 | 官方证据覆盖 | Engine 尚需完成 |
| --- | --- | --- |
| Voxel Tools CPU 路线 | GDExtension 构建矩阵含 Windows x64、macOS universal、Linux x64/arm64、Android arm64、iOS arm64 | 适配体素、数学及资源接口，逐平台编译和场景验证；上游矩阵不证明所有功能均已真机测试 |
| Transvoxel | CPU 表面/过渡算法及查找表不绑定图形 API | 已实现同步普通单元分块、存储、渲染/碰撞接入；LOD 过渡、异步任务及其他平台正式代码验证仍未完成 |
| FastNoise2 | 官方声明 Windows/Linux/macOS/Android/iOS，含 NEON；主要 CI 有桌面平台 | Android/iOS 最小编译验证；API、序列化与 Voxel Tools 旧噪声包装不能直接混用 |
| Jolt | 官方列出 Windows、macOS、Linux、Android、iOS 的对应架构，ARM64 使用 NEON | 新增瞬时碰撞与碎块能力必须单独验证，已有基础物理通过不等于新能力通过 |
| Blast 基础库/Toolkit | 官方明确 Windows/Linux x64 构建路径；头文件有 Apple、Android、ARM 条件 | low-level 已完成步骤 0 的 Mac/Android ARM64 独立验证；步骤 2 正式 Mac 定向测试与 MCP 预览已通过；已完成的菜单玩法与其他平台限制见下文。Android 独立 Graph 检查不是完整 APK，Toolkit、iOS 未验收 |
| Blast ExtStress | 现有实现使用 x86 SSE/AVX，Linux 配置还有 `-march=haswell` | ARM 原生编译存在障碍，Linux arm64 同样不能由配置名字推断可用 |
| Blast Authoring | Windows/Linux 桌面构建路径 | Mac 已编译并通过预破碎/导入相关定向测试，三份示例资源已成功导入并用于窗口预览；设备只消费独立生成资源，不部署制作工具 |

依据：[Voxel Tools 构建矩阵](https://github.com/Zylann/godot_voxel/blob/045ff9326a32fd10ac6fdac6357b5a098aaf2093/.github/workflows/extension_builds.yml)、[FastNoise2 平台声明](https://github.com/Auburn/FastNoise2/blob/721400f9560831be2fc9790be9b72c44bee7d33f/README.md)、[FastNoise2 CI](https://github.com/Auburn/FastNoise2/blob/721400f9560831be2fc9790be9b72c44bee7d33f/.github/workflows/main.yml)、[Jolt 平台说明](https://github.com/jrouwe/JoltPhysics#supported-platforms)、[Blast 构建配置](https://github.com/NVIDIAGameWorks/Blast/blob/636e7eb43b8d41e44c40eb1b4799bfd027f17ba7/premake5.lua)。

## 步骤 0 实测记录

以下保留步骤 0 的历史结果：当时只构建独立 C++20 验证程序，使用 Ninja、ccache 与目标级无异常/无 RTTI 配置；Jolt 复用已有 5.4.0 静态库。没有重编 Engine、V8、APK 或整套依赖，没有运行 CI；该阶段未修改正式构建预设、插件清单与项目场景。步骤 1/2 的后续改动与验证单列在后文。

环境：Mac 为 Apple M1 Max、macOS 27.0.1、AppleClang 21；Android 为 NDK r27c / Clang 18、目标 API 28，在 Android 14 ARM64 模拟器上执行。两端均为 Release 原生 ARM64 程序；Android 使用静态 C++ 运行库。该步骤没有测试手机性能、Windows、Linux 或 iOS。

### 地形普通单元与跨块边界

直接使用上述固定版本官方 Transvoxel 查找表，验证 256 个普通单元表项，以及两个相邻 16³ 块的平面与球形挖洞。检查顶点有限值、索引、三角形非退化和共享边两侧端点及相反绕序，两端均通过。平面共 1,024 个三角形、每侧 16 条接缝边；球形挖洞共 1,316 个三角形、每侧 26 条接缝边。

这是官方表驱动的最小验证循环，不是完整 Voxel Tools 移植；没有顶点复用优化，没有验证不同 LOD 的过渡单元、材质、存档或引擎渲染。

### Blast 基础连接损伤

仅编译 low-level 与必要 common 源码；不用 PhysX、CUDA、Toolkit、应力或 Authoring。三块支持结构 A–B–C 中，部分损伤保持连接，断开 A–B 后得到 `{A}` 与 `{B,C}`，再断开 B–C 后得到三个独立 Actor。检查 Chunk 归属，逐个释放后 Family 为空，重复 256 次；Mac 与 Android 均通过，零错误/警告回调。

发现并修正一项构建适配：上游 `NV_C_EXPORT` 在 Apple/Android 上为空，但实现使用 C 链接，初次编译报 `different language linkage`。验证目标通过 PUBLIC `NV_C_EXPORT=extern "C"` 定义同时修正库与调用方，不修改上游检出。此结论是“加该目标级配置后基础子集可用”，不是上游默认工程原样通过。Mac 上仍有上游断言代码的 `sprintf` 弃用警告。

最小资源大小为 Asset 384 B、Family 448 B、Scratch 176 B；它们仅是该三块样例的 API 内存需求，不是整个进程占用。Android 二进制动态依赖只有系统 `libm`、`libdl`、`libc`，Mac 只有系统 C++ 与系统库，没有额外 Blast/PhysX 动态库。

### ARM 应力障碍

分别用两端相同工具链对官方 `stress_solver/simd/simd.h` 做最小语法编译，均在 `xmmintrin.h` 报 `This header is only meant to be used on x86 and x64 architecture`。这证实当前应力实现不能原样进入 ARM 构建，不是运行时开关可以解决的问题。后续单独适配标量路径或成熟 SIMD 兼容方案；步骤 0 当时未展开应力移植，也未编译 Mac Authoring。步骤 2 的 Authoring 进展见后文。

### Jolt 碰撞网格安全替换

两端均验证 16×16 平面网格从 512 个三角形变为中心带洞的 504 个三角形：候选形状准备时旧地面仍可命中；无并发物理更新的安全点调用 `BodyInterface::SetShape` 后，洞口射线不命中、旁边仍命中。动态箱体在剩余地面模拟 180 步正常落地，所有更新返回成功。

首次独立程序因默认构造 `JobSystemSingleThreaded` 后没有初始化容量而崩溃；按官方接口传入 `cMaxPhysicsJobs` 后通过，并设置 Trace 回调。修改仅在验证程序，不是引擎缺陷，也没有重编 Jolt。该结果在步骤 0 仅验证 Jolt 底层能力，不能替代后续核心 Collider、地形联合提交或建筑碎块的正式集成验收。

### 微型耗时与范围

| 单项工作 | Mac ARM64 Release | Android ARM64 模拟器 |
| --- | --- | --- |
| 两块平面网格生成 | 0.097 ms | 0.179 ms |
| 两块球形挖洞网格生成 | 0.124 ms | 0.178 ms |
| Blast 两次断裂与拆分，中位数 | 0.166 μs | 0.208 μs |
| Blast 两次断裂与拆分，P95 | 0.208 μs | 0.209 μs |
| Jolt 原始 512 三角形碰撞构建 | 0.176 ms | 0.180 ms |
| Jolt 带洞 504 三角形碰撞构建 | 0.110 ms | 0.110 ms |

地形耗时为单次网格生成，不包含正确性检查、碰撞或 GPU；Blast 统计只包括两次 apply/split，不包含资源分配、创建与检查；Jolt 单次计时从 MeshShapeSettings 创建到形状构建完成，不包括输入网格生成、SetShape 或物理步进。样例很小，计时存在分辨率和宿主调度影响；模拟器不是手机，不能据此承诺大地图、高楼或连续爆炸的帧率和容量。

验证源码、CMake、固定上游检出与日志在忽略的 `build/terrain-step0/`；回执汇总在 `projects/showcase/cache/terrain-step0/`。后续步骤 1 已完成地形所属层级类图及 Transvoxel 正式依赖登记。LOD 过渡、FastNoise2、Voxel Tools 整套适配、Toolkit、离线预破碎及 Blast→Jolt 碎块接入均不在步骤 0 的通过范围内；不能以该历史列表否认步骤 2 后续完成的代码与单测。

步骤 0 决策：可以继续步骤 1 的同步地形实验场，并使用现有 Jolt 做静态碰撞更新。建筑连接损伤可采用经过上述修正的 Blast low-level 子集；应力和 Mac 制作工具保留独立验证任务，不能因基础拆分通过而直接启用完整破坏模块。这是步骤 0 当时的结论；后续步骤 1 的实际范围如下。

## 步骤 1 正式实现与验证

已实现 `runtime/terrain`、`editor/terrain` 插件及各自 CMake 清单，正式接入 Transvoxel 普通单元表、独立 `.terrain` 资源、TerrainComponent/TerrainSystem、脚本、可撤销 MCP 编辑与 showcase 第六个菜单入口。核心仅扩展通用瞬时碰撞提交和组件瞬态标记，编辑器基础不引用具体地形插件。详见[运行时类图](class-diagrams/terrain.md)与[编辑器类图](../editor/class-diagrams/terrain-editor.md)。

当前为同步实现，密度使用 `float`。脚本直接球形笔刷经 `TerrainComponent::apply_sphere` 复用缓存，仅重建脏块及梯度边界邻块；MCP 通过 `TerrainStateEdit` 将候选 ETRS 交给 `restore`，当前重新初始化体积，因此 MCP 笔刷、撤销/重做、加载和重置均重建全部块。脚本 `Load`/`Reset` 也会全块重建。两条路径最终均合并为单个 Mesh，完整上传 GPU 并进行 Jolt cook。该地形插件没有分块 GPU 更新、局部碰撞 cook、LOD 或后台任务；建筑破坏由独立的步骤 2 插件提供，不属于步骤 1。存档保存完整体积快照及基础 GUID/hash，不是差量块存档。

| 验证 | 结果与范围 |
| --- | --- |
| macOS ARM64 Debug 构建及定向 CTest | `engine.physics`、`engine.scene`、`editor.scene_persistence`、`engine.terrain_runtime`、`editor.terrain_pipeline` 五项均通过，合计 6.74 秒 |
| macOS ARM64 Release 构建及定向 CTest | 同五项均通过，合计 3.35 秒 |
| MCP 注册器补充回归 | 地形工具参数约束使用注册器支持的 `minimum`；`engine.mcp_registry` Debug 0.76 秒、Release 0.61 秒均通过，两套配置累计各六项通过 |
| 正式地形测试覆盖 | 16/32 格分块等价、接缝及跨块局部修改、渲染/碰撞提交失败保留、清空碰撞、存档恢复与基础版本不匹配拒绝、瞬态组件不进入场景保存、资源增量/失败保留及组件编解码 |
| showcase 窗口场景 | Metal 实景通过跨块挖掘、填充、撤销/重做、保存恢复、菜单往返、预览与退出空白；详见下述实景记录 |
| Android 及其他平台 | 本次正式插件代码尚未验证；上文 Android 独立步骤 0 探针通过不等于新插件或 APK 已通过 |

测试日志保留在忽略的 `build/terrain-step1/debug-tests.log`、`build/terrain-step1/release-tests.log`。未重复运行全仓库测试、V8 构建或 CI。定向测试用时是测试进程总时长，不是游戏帧耗时或地形修改延迟。

## 适配边界

### 地形

Voxel Tools 的 Transvoxel 实现已有较独立的网格输出与缓存，但仍依赖其 VoxelBuffer、数学类型、分配器和辅助代码；外层还使用 Godot Resource、ArrayMesh 等。移植保留成熟算法，资源提交改接本引擎，避免复刻一套 Godot 运行框架。参见[算法实现](https://github.com/Zylann/godot_voxel/blob/045ff9326a32fd10ac6fdac6357b5a098aaf2093/meshers/transvoxel/transvoxel.cpp)。

上游 GPU 路径依赖 Godot RenderingDevice/GLSL；细节法线对部分节点和已编辑体素有限制，不能直接视为本引擎 Metal/D3D12/Vulkan 通用实现。后续确有收益再通过 bgfx 适配。参见[GPU 与编辑限制](https://github.com/Zylann/godot_voxel/blob/045ff9326a32fd10ac6fdac6357b5a098aaf2093/doc/source/smooth_terrain.md)。

FastNoise2 的 `FASTNOISE2_STRICT_FP` 默认关闭，不能默认不同 SIMD 后端按同种子得到逐字节相同数据。首版预生成初始块并加载，存档保存实际修改与基础版本；未来运行时重新生成必须明确生成器版本及确定性要求。参见[构建选项](https://github.com/Auburn/FastNoise2/blob/721400f9560831be2fc9790be9b72c44bee7d33f/CMakeLists.txt)。

### Blast

独立仓库的 5.1.0 默认不要求 PhysX；其构建采用 repo tools/Premake，不能假设有可直接引用的 CMake 工程。已按 Engine 规则补目标级配置，接入 NvBlast 与必要 Globals、官方伤害函数和编辑器 Authoring；Toolkit 仍按需选用，不整套引入示例查看器、Exporter 和 PhysX 适配。参见[变更记录](https://github.com/NVIDIAGameWorks/Blast/blob/636e7eb43b8d41e44c40eb1b4799bfd027f17ba7/docs/CHANGELOG.md)与[README](https://github.com/NVIDIAGameWorks/Blast/blob/636e7eb43b8d41e44c40eb1b4799bfd027f17ba7/README.md)。

ExtStress 的 SIMD 头直接引用 x86 intrinsics，求解代码使用 CPU 指令探测。现存标量求解器不能通过运行时关闭 SIMD 自动避开 ARM 编译问题。可先评估隔离现有标量路径，再考虑成熟 SIMD 兼容层或 NEON 适配；这是独立移植任务，不能承诺只改宏即可。基础平台识别对 Apple ARM 也需核对 macOS/iOS 的区分。参见[SIMD 头](https://github.com/NVIDIAGameWorks/Blast/blob/636e7eb43b8d41e44c40eb1b4799bfd027f17ba7/source/shared/stress_solver/simd/simd.h)、[求解实现](https://github.com/NVIDIAGameWorks/Blast/blob/636e7eb43b8d41e44c40eb1b4799bfd027f17ba7/source/shared/stress_solver/stress.cpp)、[平台宏](https://github.com/NVIDIAGameWorks/Blast/blob/636e7eb43b8d41e44c40eb1b4799bfd027f17ba7/include/shared/NvFoundation/NvPreprocessor.h)。

应力求解耗时受支持图规模和迭代次数影响，预处理应放加载阶段；不能用细小渲染三角形数量直接定义承重图粒度。官方实验性图简化选项不作为首版可靠性前提。参见[应力扩展参数](https://github.com/NVIDIAGameWorks/Blast/blob/636e7eb43b8d41e44c40eb1b4799bfd027f17ba7/docs/api/extensions/ext_stress.rst)。

Authoring 包含几何处理和额外依赖，留在编辑器；已有 FlatBuffers 契约不因接入而强制再引入其 Cap'n Proto 序列化扩展。SDK 默认动态库目标需按 Engine 产物要求合入；Android 仍只有 `libmain.so`，不附带 PhysX、CUDA 或制作工具。

### Blast 移动端适配与社区证据

补充研究日期：2026-10-31。用户要求先找现成移动适配，减少自写算法。本节是当时的只读来源调查，仅检查公开源码、提交与报告，没有运行这些社区工程；这次调查本身未新增 Android/iOS、ExtStress 或 Authoring 测试。后续本仓步骤 2 的实现与验证另列，不与社区报告混为一项证据。不能把“仓库位于 NVIDIA 组织”或“在官方 issue 下发言”当作 NVIDIA 官方平台认证。

| 证据 | 已核对内容 | 适用范围与限制 |
| --- | --- | --- |
| [PhysX issue 360 的移动演示留言](https://github.com/NVIDIA-Omniverse/PhysX/issues/360#issuecomment-2688366260) | 社区开发者说明把 Blast 移到 CMake，适配移动端/Mac，并报告在 Google Pixel 和 iPhone 上运行了 common fracture 与运行时 Voronoi，附[演示视频](https://youtu.be/MnusdXUDvx0)；[NVIDIA 维护者回应](https://github.com/NVIDIA-Omniverse/PhysX/issues/360#issuecomment-2741677399)确认看到手机演示 | 属于社区设备演示报告。本次读取了留言，未独立复现；留言没有可锁定的移植源码、测试工程或性能原始数据，并明确说当时尚未测试 bonds generation。不能据此声称 ExtStress、完整建筑坍塌或本仓 iOS 已通过 |
| [DemiEngine 固定版本报告](https://github.com/JEAPI-DEV/DemiEngine/blob/b5f94e9e1cfc79482690c166b240e03e6ff2ed6e/docs/3d-compound-transition-feasibility.md) | 报告日期 2026-09-13，说明 Linux 与实体 Pixel 7 / Android 15 上验证 Blast low-level 拆分后替换 Jolt compound；有 16/64/256 块、守恒、碰撞、容量不足回退等检查和分阶段计时 | 源码和运行器可读；原始 CSV/日志只记在该作者忽略的 `build/`，本次不能独立核验。该报告明确不包含应力、作者工具、渲染、完整 Android 游戏或持续热负载，不能照搬其计时作为 Engine 的性能结果 |
| [Glavin001 的 ExtStress 适配](https://github.com/Glavin001/blast-stress-solver/blob/f3955688c392661d8e9bed350542f5a5cdd39383/blast/blast-stress-solver-rs/README.md) | 已提供 `aarch64-apple-darwin` 和 WASM 包装及源码构建路径；[构建说明](https://github.com/Glavin001/blast-stress-solver/blob/f3955688c392661d8e9bed350542f5a5cdd39383/blast/STRESS_SOLVER_DEMOS.md)与实现使用现有标量求解器，绕开 x86 intrinsics/CPUID | 是可复用的源代码适配证据；Apple Silicon macOS 与 WASM 不等于 iOS/Android 真机测试。当前读取的目标清单没有声明 Android/iOS ExtStress 已验证 |

DemiEngine 的[最小 CMake 接入](https://github.com/JEAPI-DEV/DemiEngine/blob/b5f94e9e1cfc79482690c166b240e03e6ff2ed6e/cmake/DemiBlast.cmake)固定 NVIDIA PhysX `4f2103c3a9052906296defb12166753450ef787c`（Blast 5.0.6），只编译 low-level/common 和 NvFoundation 头；Android 通过目标级 `NV_C_EXPORT=extern "C"` 修正链接。该工程与其 Blast 上游源码分别保留 [BSD-3-Clause](https://github.com/JEAPI-DEV/DemiEngine/blob/b5f94e9e1cfc79482690c166b240e03e6ff2ed6e/LICENSE) 及 NVIDIA BSD-3-Clause 声明。它与本仓步骤 0 发现一致，可参考依赖裁剪与拆分交接方式；不直接替换本仓已选 5.1.0，不为接入另引 PhysX。

ExtStress 原始源码本来就有 `CGNR_SISD` / `Float_Scalar`，问题是 x86 头、类型特化和 CPU 探测仍参与编译。Glavin001 的补丁保留该数值算法，通过 `STRESS_SOLVER_FORCE_SCALAR`、`STRESS_SOLVER_NO_SIMD`、`STRESS_SOLVER_NO_DEVICE_QUERY` 选择它。可锁定[源码快照 `230003d9a640927f1bd61d9cf4b88c84d51c1cc1`](https://github.com/Glavin001/blast-stress-solver/tree/230003d9a640927f1bd61d9cf4b88c84d51c1cc1/blast/source/shared/stress_solver)作为早期移植参考，按相关文件提取必要兼容改动，避免同时带入后续优化及 Rust/Rapier 框架。关键提交来源：

- [`a954c98f56053fe79dc8cfb4f824ae08706fffde`](https://github.com/Glavin001/blast-stress-solver/commit/a954c98f56053fe79dc8cfb4f824ae08706fffde)：允许强制标量选择，补 WASM 指令探测处理。
- [`b0c090fee591396d6ca6632d4375d8f615a472ed`](https://github.com/Glavin001/blast-stress-solver/commit/b0c090fee591396d6ca6632d4375d8f615a472ed)：在 `solver_types.h`、`anglin6.h`、`inertia.h`、`coupling.h`、`math/cgnr.h` 隔离 SIMD 实现，复用标量运算。
- [`230003d9a640927f1bd61d9cf4b88c84d51c1cc1`](https://github.com/Glavin001/blast-stress-solver/commit/230003d9a640927f1bd61d9cf4b88c84d51c1cc1)：补 macOS 平台/计时、`NsFPU.h` 标量 guard 以及 `simd_device_query.h` 禁用探测路径。该快照包含此前累积改动，不能只应用这一条提交就视为完整适配。

上述 SDK 修改文件仍保留 NVIDIA BSD-3-Clause 头，见[许可证](https://github.com/Glavin001/blast-stress-solver/blob/f3955688c392661d8e9bed350542f5a5cdd39383/blast/PACKAGE-LICENSES/blast-sdk-LICENSE.md)。正式采用时仍需对照本仓 Blast 固定版本审查差异，把补丁及来源放入 `third_party/`，由构建在编译上游代码前应用；这次来源调查只记录 ExtStress 补丁，没有声称应力适配已移植或验证。

SIMD 兼容库作为后续测量后的备选：

- [sse2neon `2272251cc3ad9cba8b8c47c4065811cfb396b802`](https://github.com/DLTcollab/sse2neon/tree/2272251cc3ad9cba8b8c47c4065811cfb396b802)，MIT：覆盖 SSE 到 ARM NEON；Blast 当前还使用 256-bit AVX/FMA，不能只替换 SSE 头就宣称整套可用，亦未找到可核验的 Blast 移动适配测试。
- [SIMDe `65c15e6b545361f6dd89b1e3bcc76273e1344653`](https://github.com/simd-everywhere/simde/tree/65c15e6b545361f6dd89b1e3bcc76273e1344653)，MIT：有 SSE/AVX/FMA 的可移植实现及 ARM CI，可作为后续优化候选；本次未找到已有 Blast 结合该库的 Android/iOS 设备验收，不能把 SIMDe 自身测试视为 Blast 测试。

步骤 2 已采用官方 Blast 损伤/连接拆分与 Authoring，Jolt 处理凸形/复合碰撞。预破碎制作留在编辑器，设备消费独立生成资源；不自写 Voronoi、应力求解或新物理引擎。ExtStress 后续优先适配上述现成标量路径，再根据真实设备测量决定是否引入 SIMD 兼容库；iOS、本仓正式 Android 破坏插件、Mac Authoring 与 ExtStress 的通过状态都必须单独记录。

### 步骤 1 实景记录

macOS ARM64 / Metal / Release / QuickJS 下，64×64×32 米实验体积实际生成 32 块、初始 24356 个三角形。固定笔刷将隧道推进至 z=-18，穿过 z=0 和 z=-16 块边界；挖坑、填充、撤销/重做、颜色修改保留体积、存档加载及重置均已执行，ETRS 全量哈希验证恢复一致。脚本 UI 直接挖掘仅重建 4/32 块，UI Save→Reset→Load 也恢复相同哈希；这不是大地图/连续爆炸吞吐测试。

首次预览发现宿主错误地直接 Tick 子系统，现改用插件公开同步接口，并保留核心生命周期限制。修复后真实预览生成地形，关闭预览返回空场景、零节点和空白窗口，重新运行项目正常；新增实际子系统装配回归在 Debug/Release 分别 2.26/0.75 秒通过。场景阴影偏移调整到 0.004，解决初始参数产生的主要条纹，没有修改渲染算法。过程证据保留在 `build/terrain-step1/` 的 JSON/JSONL 回执及 `projects/showcase/cache/screenshots/`。本次未做正式 Android 插件或其他平台运行验收。

## 步骤 2 当前实现与验证范围

`runtime/destruction` / `editor/destruction` 已按清单接入固定 Blast 5.1.0。运行时使用官方 low-level 和 `NvBlastExtFalloffGraphShader`；编辑器使用官方切片/Voronoi、三角网格体积积分及 `bondsFromPrefractured(EXACT)` 接触生成。EXACT 路径以三角面实际接触建立连接，避免把仅边/点接触误当有效支撑面；导入 recipe 指纹已升级，使早期缓存重新生成。

参数盒体生成独立破坏资源，最多 256 块。每个连通组拥有一个瞬态渲染/碰撞节点，使用 Jolt 凸包/compound；根节点和正等比缩放是当前限制。完整快照支持损伤、重置与设置的撤销，保留连接血量、组姿态和速度。当前仍同步重建网格与碰撞，没有地形地基联动、碰撞再损伤、承重应力或后台调度。

| 验证层级 | 当前记录 |
| --- | --- |
| macOS 正式代码 | Debug 与 Release 各 9 项定向 CTest 全部通过，测试进程总时长分别 4.12 / 4.33 秒；不是游戏帧耗时 |
| Android ARM64 | 已进行独立 `DestructionGraph` 运行检查；不包含完整引擎宿主、Jolt/渲染集成、UI 或 APK 场景验收，也不是实体手机性能测试 |
| 其他范围 | iOS、Windows、Linux 的本轮正式插件集成验收尚未完成；ExtStress 未接入 |
| showcase 导入与预览 | 最终三份资源 ready，块数为 48 / 24 / 8。MCP 预览中墙体从 1 组拆成 6 组，其中 5 组为动态；undo 回 1 组，redo 回 6 组。关闭预览后 scene 为空，窗口截图已保存 |
| 正常菜单与 UI | 最终 TS 刷新后，由正常菜单第七入口进入；初始共 3 组、0 动态。column 后柱为 3 个动态组并实际倾倒；wall 后墙为 6 组、5 动态；top 后塔为 15 组、14 动态；base 后塔为 24 组、24 动态，三对象合计 33 组、32 动态。reset 后三对象各恢复 1 组、0 动态 |
| 源节点生命周期补充 | 新增源 Transform 禁用回归，仅重跑 `engine.destruction_runtime`，Debug 0.87 秒、Release 0.46 秒通过 |
| 镜头与退出 | front/overview 切换及 Back 返回 `Scene launcher` 均通过；停止项目后 scene 为空、节点数为 0，已保存空白窗口截图 |

九项测试为 `engine.physics`、`engine.mcp_registry`、`engine.scene`、`editor.scene_persistence`、`engine.destruction_graph`、`engine.destruction_runtime`、`editor.destruction_authoring`、`editor.destruction_pipeline` 与 `editor.destruction_commands`。测试日志在忽略的 `build/terrain-step2-tests-debug.log` 和 `build/terrain-step2-tests-release.log`。

最终源码的 Android ARM64 独立 Graph 增量编译及执行再次通过（exit 0）；二进制和源码指纹见
`build/destruction-step2/android-final-result.json`，不扩大为完整 APK 或设备性能结论。

预览证据在 `build/terrain-step2/preview-ready-report.json`、`preview-damage-report.json` 和 `preview-history-report.json`；截图为 `projects/showcase/cache/screenshots/destruction-step2-preview-ready.png`、`destruction-step2-preview-damage.png` 与 `destruction-step2-closed-preview.png`。这些是本机真实窗口记录，不是生成图。最终脚本版本的正常玩法证据为 `build/terrain-step2/finish-*-report.json`（before、column、wall、top、base、reset、front、overview、back、stop）；代表截图为 `destruction-step2-finish-before.png`、`destruction-step2-finish-base.png`、`destruction-step2-finish-reset.png`、`destruction-step2-finish-back.png`，同样位于 showcase 的 cache/screenshots。较早的 `final-*` 回执保留为调参过程，不替代这些最终结果。

演示中原始居中爆心会让已经断键的块继续通过碰撞互相支撑，因此图拆分不等于所有块必然立即散开。仅在项目脚本中调整爆心/冲量：柱试件使用 20000 的冲量、塔底使用 60000；同参 MCP 检查与随后刷新脚本后的 UI 流程已观察到柱倒下和塔体下沉。该结果验证显式损伤与刚体接触的组合，不是高楼完整承重、ExtStress 或地基地形联动的验收，具体参数也不是引擎内置特例。

步骤 0 的微型计时和社区设备报告不能替代这一阶段的正式结果。最终运行证据由根 README 及本阶段回执统一记录；既不触发 V8 重编，也不以自动 CI 扩大验证范围。

## 性能判断及当前接入缺口

以下是源码证据与工程推断，不是 FPS 或设备容量实测。整个链路都需观察，不能把噪声吞吐量等同于地形性能。表中线程、任务队列和发布预算属于同步功能完成后的优化，不提前建设。

| 环节 | 主要成本与拟处理方式 |
| --- | --- |
| 体积修改/网格生成 | 限定脏块、复用缓存、合并重复修改；先比较 16³、32³ 两种块规格，不做大规模参数排列 |
| 碰撞准备 | 先同步准备并安全替换，独立记录构建时间；后续后台化时，网格完成不代表碰撞已可用 |
| 主线程提交 | 限制每帧发布工作量，过期任务丢弃；保持显示与碰撞版本一致，记录修改生效延迟 |
| GPU 更新 | 避免同帧集中上传/释放大量网格；是否增加局部更新接口由数据决定 |
| 建筑碎块 | 离线预破碎、凸形/复合碰撞、批量创建、睡眠；活跃刚体和接触数量比模型面数更能说明物理负载 |
| 任务与内存 | 有限在途任务、块快照与结果缓存；连续爆炸不能无限积压旧版本 |
| 导航/表现 | 局部导航重建与特效分别计时，不让地形、物理、导航同时无预算占满工作线程 |

上游性能讨论也强调碰撞准备、主线程提交、网格数量和任务预算；其 Godot 条件下的具体倍数不能当成 Engine/Jolt 测量。参见[Voxel Tools 性能说明](https://github.com/Zylann/godot_voxel/blob/045ff9326a32fd10ac6fdac6357b5a098aaf2093/doc/source/performance.md)。Jolt 有后台批量准备及提交接口，可复用而非另造物理调度逻辑；动态三角网格有碰撞限制及性能代价，碎块优先使用凸形/复合形状。参见[Jolt 架构](https://jrouwe.github.io/JoltPhysics/)。

当前 Engine 核对结果：

- `MeshRenderer::set_mesh_data` 完整复制数据；revision 更新会重建 GPU 几何，包含切线计算与整块上传，并非已有局部顶点更新。见 [mesh_renderer.cpp](../../../src/engine/runtime/scene/mesh_renderer.cpp)、[render_scene_resources.cpp](../../../src/engine/runtime/render/render_scene_resources.cpp)、[gpu_geometry.cpp](../../../src/engine/runtime/render/resources/gpu_geometry.cpp)。
- `PhysicsSystem` 仍使用单线程 JobSystem 与 `TempAllocatorMalloc`。新增 `set_mesh_data` 先同步准备候选，兼容静态体使用 `SetShape`，否则先创建新体再移除旧体；清空网格可移除碰撞，错误不替换有效状态。整份网格 cook 和提交仍在调用线程，后续再后台化并按实际负载评估上游线程池。见 [physics_system.cpp](../../../src/engine/runtime/physics/physics_system.cpp)。
- 三角网格碰撞继续仅允许静态用途；步骤 2 的动态碎块使用新增凸形/compound。总刚体/角色上限 4096 是当前配置约束，不是已实测支持 4096 活跃碎块的承诺。
- [NavigationSystem](../../../src/engine/runtime/navigation/navigation_system.cpp) 只初始化依赖对象，尚无实际寻路及局部重建；[TaskSystem](../../../src/engine/editor/task/task_system.cpp) 是编辑器任务系统，调用后同步等待；[PerformanceSystem](../../../src/engine/runtime/performance/performance_system.cpp) 的 Tracy 接入仍无这些阶段的采样。

### 内存量级示例

假设测试区 64×64×32 米，采样间距 0.5 米，每采样点暂按密度 16 bit＋材质 16 bit 估算：`128×128×64×4` 约 4 MiB。采样间距减半为 0.25 米，原始量约 32 MiB，即 8 倍。

这是步骤 0 假设的样本量估算，忽略边界点且不是运行峰值。步骤 1 实际 schema 使用每点 32 bit 浮点密度，尚无每点材质，样本还需包括边界点；总占用仍要另计网格、碰撞、LOD、接缝、快照、GPU 数据及碎块。它说明精度必须作为项目参数，不能把整个世界长期保持最高精度和完整副本。

## 首个项目内的分步实现与测量

所有场景位于 `projects/showcase/`，复用现有角色、菜单、UI 和特效。自建山地、洞穴、墙体、多层楼，不使用其他项目的商业素材。菜单、桌面和 Android 资源清单在对应阶段同时登记，保留原有测试场景。

| 步骤 | 内容 | 验收结果 |
| --- | --- | --- |
| 0. 技术验证 | 先用最小算法/资源验证 Mac ARM64、Android ARM64 的地形和 Blast 基础子集；应力、Mac Authoring 分开验证 | 列清能编译、能运行、尚未覆盖的平台；不触发整套依赖重编 |
| 1. 地形实验场 | 已实现 64×64 米、32 米高体积，丘陵、洞穴、跨块隧道及球形挖掘/填充 | Debug/Release 定向测试通过；macOS Metal 窗口验收通过，正式 Android 插件尚未验证 |
| 2. 建筑破坏场 | 已创建实心塔、Voronoi 墙、分段柱与固定损伤按钮；空心多层建筑输入尚未支持 | Mac 定向测试、MCP 窗口预览及最终菜单/UI 玩法均已通过；详见上文步骤 2，Android 独立 Graph 不等于 APK 通过 |
| 3. 综合联动场 | 挖地基、建筑坍塌、撞击再破坏、角色行走、烟尘与声音 | 可进入新洞穴、踩上残骸、重置与保存恢复 |
| 4. 完整玩法 | 土石塌方、跨平台支撑应力、导航局部更新 | 不因某平台缺库而悄悄关闭已承诺的玩法能力 |
| 5. 异步与扩量 | 功能完成后将重计算移到工作线程，同一场景加入连续爆炸和更多对象 | 对比同步基线，校验线程安全、版本和取消，再依据设备结果确定精度、数量及预算 |

固定种子、已发布资源版本、镜头路径和操作序列，用短流程重复对比。性能使用 Release，Debug 用于正确性检查；记录设备、后端、编译配置和帧率上限，将限帧等待与实际工作耗时分开。Mac 首跑、Android 实机复核；模拟器只能证明其环境下的功能，不代表手机性能。Windows/Linux/iOS 按实际可用环境补充，没测到的明确标记。

每阶段记录帧耗时 P50/P95/P99 与最大值，生成、meshing、碰撞准备、提交各阶段耗时，修改到可见/可碰撞的延迟，CPU 峰值内存、GPU 上传量、活跃块/三角形、活跃刚体/接触数量。同步首版不要求队列指标；异步阶段再增加任务峰值、过期结果与取消统计。数值来自同一短场景的实测；不预先承诺手机同时炸几栋楼，也不重复全仓库测试。

上述是逐步建立的测量清单，按当前阶段选择必要项；同步功能首版可先记录操作总耗时、网格/碰撞准备耗时与峰值内存，不把完整百分位采样或性能框架作为功能实现前置条件。

步骤 0、步骤 1 和步骤 2 的验证范围分别见上文。地形实验场已完成 Mac 验收，建筑实验场已完成本轮 Mac 定向测试与窗口流程验收，综合联动场留待步骤 3；微型程序与无窗口测试都不能替代引擎窗口场景及实体设备验收。
