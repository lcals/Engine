# 可修改体积地形与建筑破坏方案

更新日期：2026-11-01。同步地形、建筑破坏、地基/撞击联动、重力应力、土石提取、静态导航和 showcase 火箭场已实现；本轮单线程碰撞优化已经结束。当前采用 4³ 单元碰撞叶与连续 64 槽组树缓存，渲染仍完整上传。最终增量碰撞样本满足单次 <0.5 ms，Jolt 单步及整体物理仍未达到该目标，数值统一见[最终可信基线](../../runtime/performance.md#最终可信基线)。

全部功能场景位于 `projects/showcase/`。macOS 已完成记录中的定向测试与窗口流程；Android ARM64 只有部分独立算法/Graph/应力探针通过，不能作为正式插件、APK 或实体手机性能验收。其他平台范围、阶段结果与未完成能力见[跨平台记录](terrain-destruction-feasibility.md)，制作流程见[编辑器方案](../editor/terrain-destruction-authoring.md)。

## 目标与数据选择

- 割草玩法中的整张地形可修改，支持挖掘、填充、爆炸、地下洞穴、隧道和悬挑。
- 高层建筑支持局部掉块、连接破坏与失去基础支撑后的坍塌，掉落部分参与碰撞，并可通过撞击继续造成破坏。
- 地形变化、建筑损伤和必要的碎块状态可保存、恢复；烟尘与声音使用已有特效和音频系统。
- 地形采用分块密度/SDF 与材质数据；建筑保留模型、碎块和连接关系。初始高度图可以转换成体积数据，运行时不能只保留无法表达地下空间的高度图。
- 地形网格生成不会自动产生土石塌方。悬空土石需要单独识别脱离的连通区域，再转换为动态碎块。

## 开源来源与复用边界

| 任务 | 来源 | 复用边界 |
| --- | --- | --- |
| 体积连通与浮岛提取 | [Voxel Tools](https://github.com/Zylann/godot_voxel) | 适配固定版本的算法和数据处理，不引入整个 Godot |
| 表面网格及 LOD 过渡算法 | [Transvoxel](https://github.com/EricLengyel/Transvoxel) | 已采用 regular-cell 查找表；LOD transition 尚未实现 |
| 程序地形噪声 | 可选 [FastNoise2](https://github.com/Auburn/FastNoise2) | 仍为候选，不替代地形资源流程 |
| 预破碎、连接损伤与应力 | [Blast](https://nvidia-omniverse.github.io/PhysX/blast/) | 已接入 low-level、官方 Authoring 与 ExtStress 标量路径，Jolt 桥接由插件完成 |
| 刚体、碰撞和角色运动 | [Jolt](https://github.com/jrouwe/JoltPhysics) | 继续使用现有物理世界及官方形状，不替换物理引擎 |
| 烟尘、碎屑和声音 | 现有 Effekseer、音频系统 | 根据破坏事件驱动已有组件 |

开源实现固定版本并保留来源与许可，具体版本见[核对基线](terrain-destruction-feasibility.md#核对基线)，Voxel Tools 的完整 MIT 声明见[许可记录](../../../third_party/notices/voxel-tools-terrain.md)。商业/私有参考源码、实现和资源不得进入仓库。

### Blast 与 Jolt 的分工及待验证项

Blast 底层与 Toolkit 不绑定物理或渲染库，可根据碎块连接损伤输出结构拆分；Jolt 负责拆分后刚体的运动、碰撞和冲量。我们需要将拆分结果转换为 Jolt 对象，并把碰撞反馈提供给破坏处理。

官方提供 PhysX 接入参考和独立的应力扩展，但不等于已提供本引擎的 Jolt 接入。独立 Blast 5.1.0 默认不要求 PhysX，官方构建仍主要覆盖 Windows/Linux x64。步骤 0 已通过目标级 `NV_C_EXPORT` 修正，在 Mac、Android ARM64 验证 low-level 三块连接体的损伤与拆分。ExtStress 的 x86 SIMD 头在步骤 0 的两个 ARM 工具链上均编译失败，步骤 4 已参考固定社区快照隔离 x86 专用代码并接入原标量求解器；Mac 正式定向测试及 Android 独立探针通过，完整移动宿主仍未验收。步骤 2 已接入官方 ExtShaders 和 ExtAuthoring，Mac 定向测试与 MCP 预览已通过，具体范围见上述步骤 2 验证记录。不能把基础子集通过当作 Toolkit、应力或整套库全平台完成，也不更换 Jolt。

连接检查首先解决“结构已断开”；承重与应力进一步解决“仍然相连但支撑不足”。首阶段不宣称完整建筑力学模拟；步骤 4 的应力验证只覆盖现有支撑图、重力载荷和显式材料阈值。

## 模块与依赖边界

地形与建筑破坏均按运行时和编辑器插件拆分；宿主按构建清单装配。

| 位置 | 职责 |
| --- | --- |
| 运行时核心 | 通用动态网格、碰撞数据提交与安全替换、物理查询等底层能力，不识别地形玩法或具体破坏库 |
| `plugins/runtime/terrain/` | 体积/分块球形修改、网格/顶点色、静态状态保存及显式悬空土石提取，动态土石存档未实现 |
| `plugins/runtime/destruction/` | 官方 Blast 连接损伤、支撑图拆分、重力应力、瞬态组与 Jolt 刚体，提供建筑图/组状态快照 |
| `plugins/editor/terrain/` | 已实现地形源导入、组件编解码、创建资源、可撤销设置/笔刷/恢复与 MCP 适配 |
| `plugins/editor/destruction/` | 已实现官方盒体切片/Voronoi 预破碎、资源导入、组件 codec、可撤销设置/损伤/重置与 MCP |
| 项目业务或宿主调用方 | 发起修改与损伤，组织地形支撑变化、建筑破坏、撞击和表现的联动 |

场景使用普通 Node 挂载对应组件并绑定资源。系统遵循 System 生命周期；Node、核心和编辑器基础不引用具体插件。两个运行时插件不形成循环依赖，通过调用方传递明确的变化结果。项目配置决定爆炸、伤害和表现，不在引擎写特定地图、建筑或模型的特殊处理。

每个插件使用独立 CMake 清单，由宿主按选择装配；未选插件不准备其依赖。运行包不含 MCP、编辑器或离线破碎工具。第三方类型私有于实现；数据继续使用既有 FlatBuffers 契约，不另造一套持久化模型。

地形已实现的契约见[运行时类图](class-diagrams/terrain.md)、[编辑器类图](../editor/class-diagrams/terrain-editor.md)和[脚本接口](class-diagrams/terrain-script-api.md)。建筑契约见[破坏组件与系统](class-diagrams/destruction.md)、[Blast 图](class-diagrams/destruction-graph.md)、[预破碎](../editor/class-diagrams/destruction-authoring.md)与[破坏编辑器](../editor/class-diagrams/destruction-editor.md)。后续联动与异步能力继续先更新所属层级类图，再实现。

## 运行流程

### 同步准备与安全提交

当前地形生成、修改、网格和碰撞准备、建筑破坏均同步执行，已在 showcase 验证完整流程。本轮没有新增线程池或异步队列。

实现保持“计算生成结果”和“向渲染/物理世界提交结果”两个清晰步骤。计算只读明确的输入并拥有输出，不直接修改活动场景、GPU 或物理世界；同步版本在当前调用中完成准备，再在安全时点提交。失败保留旧有效状态，显示与碰撞保持同一版本。这里只划分职责，不提前建立通用任务框架。

后台准备属于后续候选工作；只有选定目标后再依据实测安排，提交仍须遵守渲染和物理系统线程约束。若采用，需要输入快照、版本校验、取消和队列预算，不能把包含场景操作的整段事务直接移到后台。

### 地形修改

1. 调用方提交球形挖掘或填充请求，修改受影响的密度样本；项目通过球形挖掘表达爆炸，材质笔刷留待后续。
2. 脚本 `apply_sphere` 复用粗块缓存，重建脏块及梯度邻块；MCP 事务/restore、撤销/重做、加载与 Reset 仍从快照或初始资源重建全部粗块。渲染合并为单 Mesh 并完整上传；碰撞按 4³ 单元叶子组织，未变位置/索引复用不可变快照，核心复用叶子与连续 64 槽组树。局部路径不再完整 cook 地形，详见[碰撞方案](terrain-collision-plan.md)。
3. 在安全时点提交匹配版本的显示与碰撞，失败保留原有有效状态；异步阶段再校验任务版本，拒绝过期结果覆盖新修改。
4. 通过返回状态、revision、重建块数及三角形数报告结果；步骤 3/4 由项目明确刷新建筑支撑并提交导航脏区。

步骤 4 已提供显式连通区域提取：剩余地形使用静态网格碰撞，脱离下/侧边界的土石按预算切分并使用 Jolt 凸形 compound。候选全部准备后才删除已转换样本，失败保留原土石；不是把整张地形网格直接变成动态刚体。

### 建筑损伤与地基联动

当前已实现显式球形损伤、支撑连接断裂、连通组拆分与 Jolt 模拟。步骤 3 已通过显式调用方连接地形支撑、撞击和表现，流程为：

1. 爆炸同时产生明确的地形修改请求和建筑损伤请求。
2. 地形修改生效后，重新判断受影响地基支撑区域，撤销不再有效的支撑连接；支撑判断不能只检查某一点仍有土。
3. 破坏插件计算连接断裂与结构拆分，将失去有效支撑的部分交给核心物理能力创建动态对象。
4. Jolt 模拟下落、旋转及撞击；调用方根据撞击结果决定后续损伤，避免无控制地递归触发。
5. 破坏事件驱动 Effekseer 烟尘、碎屑与音频，表现不成为地形或破坏求解的依赖。

步骤 4 由调用方提交当前全部静态几何和覆盖新旧变更的 dirty AABB，核心导航只重建相关 Recast 瓦片；失败明确失效相关旧瓦片，项目隐藏待更新路线。移动碎块导航、敌人移动及避让仍未提供。

## 资源、保存与当前缺口

初始体积块、破坏描述、Mesh、碰撞等继续作为独立运行资源，使用 GUID、真实依赖与现有打包流程；资源分组不能使未引用内容全部进入运行包。当前地形由 `terrain.fbs` 定义初始体积、`terrain_state.fbs` 定义修改状态；建筑由 `destruction.fbs` 保存初始碎块网格、体积、质心、支撑及连接，`destruction_state.fbs` 保存完整图状态和运动组状态；运行时消费平台无关数据，不保存 Blast 内存镜像。

原始生成资源与游戏存档分离。当前 `TerrainStateData` 保存完整体积快照及基础资源 GUID/hash，加载校验基础身份与数据后重新提交显示和碰撞，不覆盖原始资源。建筑当前通过快照保存基础 GUID/hash、连接血量、块分组、世界变换、线速度与角速度，用于编辑器撤销/重做；独立建筑游戏存档槽 API 尚未提供，地形仅保存修改块也仍属后续目标。编辑器快照位于 `cache/terrain-states/`；脚本使用 App 持久目录中的命名存档槽。

核心提供 `PhysicsSystem::set_mesh_data` 与 `set_mesh_parts`，分别提交单网格和不可变分段列表；候选全部成功后安全替换，兼容静态体复用 Jolt `SetShape`，全空几何清除碰撞，失败保留旧状态。地形使用现有 MeshRenderer 与分段碰撞接口，核心不包含地形策略。生成的 MeshRenderer/Collider 标记为瞬态，场景保存只记录 TerrainComponent 的资源与配置。后台准备留到后续优化；runtime/task 已提供 oneTBB 独立任务域，但地形准备尚未迁移，不能把调度底座接入当成地形已经异步。步骤 4 的 NavigationSystem 已实现显式构建、局部瓦片替换和只返回完整路径的 Detour 查询；没有自动场景扫描、移动残骸更新或代理避让。发生土石塌方后，静态 TerrainStateData 无法完整记录动态土石，snapshot/save/restore/load 明确拒绝直到显式 Reset 或更换源资源，不用假存档掩盖缺失。

## 步骤 1 当前范围

- Transvoxel 官方 regular-cell 查找表固定于 `51a494f03c5b024cd153b596bcc7152eb3cc93a6`，通过 `third_party/transvoxel` 子模块复用；未实现 LOD transition cells，未引入整个 Godot。
- `.terrain` 描述提供尺寸、采样间距、16/32 格分块、起点、种子、平面/正弦起伏及球形洞穴，导入后生成独立 GUID 体积资源。当前密度为 `float`，每轴最多 256 格且总样本不超过 4M；暂无逐样本材质层、FastNoise2 或高度图导入。
- 脚本球形笔刷复用 CPU 粗块缓存；MCP 事务提交/撤销、加载与 Reset 仍全块重建。步骤 5 后碰撞按细叶增量准备并复用组树，渲染仍合并为完整 Mesh 上传。同步链路占用调用线程，首次建图和大地图性能尚未完成验收。
- showcase 第六入口 `Terrain laboratory` 提供 64×64 米、32 米高的自制地形，以及挖隧道、爆炸挖坑、填充、保存/恢复、重置与第三人称行走。复用原有示例角色。
- 步骤 1 当时不含土石塌方、应力或导航；它们已在下述步骤 4 接入。异步调度仍未实现。

## 步骤 2 当前范围

- `runtime/destruction` 使用固定版本 Blast 5.1 low-level 与官方 `NvBlastExtFalloffGraphShader`，支持部分连接损伤、锚点支撑断开与连通组拆分；步骤 2 当时不含 ExtStress，步骤 4 后续接入。一直不依赖 PhysX。核心只增加通用 Jolt 凸包/复合碰撞能力。
- `editor/destruction` 使用官方 ExtAuthoring 切片、Voronoi、三角体积积分与 EXACT 接触连接生成器，不自写同类算法；Boost Multiprecision 只参与编辑器离线制作。
- `.destruction` 首版输入为参数盒体，`size` 各轴最多 1000，切片总块数或 Voronoi 站点数最多 256；不宣称支持任意 FBX、空心多层楼、层级碎裂或运行时重切割。支撑取局部底部范围；步骤 3 由调用方在地形修改后显式刷新地基覆盖。
- 普通根节点挂载 `DestructionComponent`，要求正等比缩放；每个连通组生成一个瞬态 `Node + MeshRenderer + Collider(compound) + RigidBody`，组内多块保持一个刚体，失去支撑后受重力和显式冲量作用。场景文件只保存源组件和设置。
- C++/脚本提供世界坐标球形损伤与重置；MCP 提供资源创建、设置、查询、损伤和重置。损伤、重置及设置通过完整快照撤销重做；普通物理运动不改变损伤 revision，外部显式修改会使旧编辑记录失效。
- showcase 第七入口 `Destruction laboratory` 提供 48 块、8 层切片塔试件，24 块 Voronoi 墙和 8 段柱，另有自制地面、灯光、两台固定相机及触屏按钮。它们是验证支撑与碎块行为的实心试件，不是已完成的真实空心建筑系统。
- 步骤 2 的初版损伤提交同步重建组网格与碰撞；步骤 5 已改为按组增量发布，兼容未变组复用节点、网格和碰撞。步骤 3/4 的撞击再损伤、烟尘声音、同步应力、导航和有界炮击继续使用同一套插件。异步调度及性能扩量仍属后续。
- 验证明细统一见[步骤 2 验证记录](terrain-destruction-feasibility.md#步骤-2-当前实现与验证范围)，区分已通过的 Mac 定向测试/MCP 预览/菜单玩法与独立 ARM 探针；不宣称 Android/iOS/Windows 已完成正式集成验收。

## 步骤 3 综合联动

- 核心提供 `PhysicsSystem::cast_ray(Collider, origin, displacement, fraction)`，使用 Jolt 当前已提交碰撞和明确的目标过滤，不把建筑/地形识别放入核心。
- `PhysicsSystem::impacts()` 提供上一物理帧的非消费只读快照，按节点对聚合本帧多子步的最强估算。法向接近速度及 Jolt 官方 `EstimateCollisionResponse` 的**估算**冲量在求解前采集，不是求解后的真实冲量。回调不执行脚本或修改场景；删除、禁用、切场景和停止清理旧事件。传感器及 CharacterVirtual 事件不在当前范围。
- 破坏插件的 `RefreshSupport` 对原锚定碎块实际底面三角形做三点面积加权采样，用指定静态 Collider 的世界向下射线检查覆盖，再以官方 Blast ApplyFracture/Split 精确解除失效 world bonds。它只近似地基覆盖，不是应力或承重计算；当前针对平面底部试件、一个支撑 Collider。
- `OwnsFragment` 只在破坏插件查询当前生成组归属。地形与破坏不相互包含或链接。项目在成功修改地形并提交碰撞后显式刷新；填土不会自动重新锚定，重置恢复原始状态。
- 脚本 `Physics.GetImpacts()`、`DestructionComponent.RefreshSupport/OwnsFragment`、可选 `EffectComponent` 播放控制共用现有 VM 绑定接口。音频继续使用 AudioSource。宿主只追加注入核心 PhysicsSystem，不引入事件总线或第三方通用包装。
- MCP 增加 `destruction_refresh_support`，复用前后状态快照撤销/重做。没有支撑变化时历史为空操作，撤销不会倒回碎块的物理运动。
- Showcase 第八入口 `Combined destruction laboratory` 使用平坦体积地形、原有角色和 48/24/8 块试件，提供挖地基、爆破、填土、重置、三镜头与行走。二次损伤按帧去重、门槛和源冷却处理，每帧最多 2 次、重置间最多 24 次；表现使用有限节点池。灰尘/爆破由固定版本 Effekseer 官方工具导出，声音为自制合成资源。
- 本阶段同步执行；不包含土石塌方、ExtStress、导航、异步或大规模性能验收。综合世界游戏存档仍未提供，现有单模块快照与编辑器撤销契约保留。

## 步骤 4 应力、土石、导航与火箭场

- **建筑应力**：复用官方 ExtStress 的 `CGNR_SISD` / `Float_Scalar`，参考固定社区补丁隔离 x86 intrinsics/CPUID，所有平台选择相同标量路径。插件将项目连接血量映射为剩余物理承载面积，求解结果再映射回血量，调用官方损伤与拆分。重力按实例旋转/等比缩放变换；无损伤不重建组。world-bond 原点问题的最小兼容修正与回归依据记录于 `third_party/patches/blast/README.md`。不加入 PhysX、不自写应力求解器。
- **土石塌方**：参考固定 Voxel Tools 连通域/浮岛提取流程，使用六邻域判断下/侧边界锚定；小岛阈值、碎块尺寸、总预算和密度由调用者传入。现有 Transvoxel 生成闭合表面，逐单元 Jolt 凸包 compound 保留单元之间的凹形。失败不丢土，继续挖填保留动态碎块，Reset 清理。当前只接受根节点正等比缩放，运行时总上限 128；项目选用 96。
- **导航**：核心 `NavigationSystem` 接收显式 MeshData/世界矩阵/范围，复用 Recast 1.6 `Sample_TileMesh` 与 Detour 的栅格化、过滤、侵蚀、轮廓、多边形和查询流程。`Replace` 重建脏区及必要边界瓦片；失败失效相关旧瓦片，`FindPath` 只返回完整路径。脚本 `Navigation.Build/Replace/FindPath/Clear` 由同一 VM 中间层绑定，项目当前只提交静态地形。
- **综合火箭场**：只增强第八场景，保留第六/七实验场。塔/墙/柱/梁资产为 256/128/64/72 块，共 520 个预破碎片段；动态刚体数量由实际连通组决定。火箭池 64 槽，2/6/12 发每秒、默认 6，60 发齐射可停止，每帧至多新发一枚且不追赶积压。每段飞行通过实际球形扫掠命中，联动建筑损伤、挖土、塌方、支撑、特效和导航脏区；不把目标按钮的坐标直接当爆点。
- **项目预算**：土石总活动量最多 96；8 个灰尘和 6 个爆破表现槽。已有每帧 2 次、重置间 24 次二次碰撞损伤预算保留。应力按项目 0.5 秒节奏显式请求，导航按 0.25 秒节奏合并脏区；均仍同步执行，卡顿后不补调用队列。普通结构阈值已调为 `1e8/2e8`，Weak beam 使用 `10/20`；切换项目强度不自动恢复旧断键。
- **测量与限制**：面板改用 `Date.now()` 墙钟帧间隔的指数平均和最大值，包含限帧及截图开销；不使用宿主截断到 0.1 秒的 `Time.deltaTime` 隐藏长帧。连射每轮清帧间隔统计，每秒及最终完成输出 `Rocket range sample` JSON，含 actualRate、命中、blocked、碎块/土石和导航状态；不是分阶段 CPU/GPU 耗时或 P95。Mac Debug/Release 各 8 项定向测试及 Android ARM64 独立应力探针通过，步骤 4 Mac 真窗口已验证两档连射及退出清理，记录了明显同步长帧。尚无综合世界存档、动态残骸导航、异步任务、完整空心建筑力学或实体手机性能结果。

实现与来源见[地形](class-diagrams/terrain.md)、[破坏](class-diagrams/destruction.md)、[导航](../../runtime/class-diagrams/navigation-system.md)、[项目说明](../../../projects/showcase/destruction-integration.md)和[验证记录](terrain-destruction-feasibility.md#步骤-4-应力土石导航与火箭场)。

## 步骤 5 单线程性能优化

已复用 `PerformanceSystem` 接入可选 Tracy CPU zone、完整帧标记与 bgfx GPU/资源统计，并在 `scripts/performance/` 固化采集、活动窗口和预算分析流程。Scene 索引/子树删除、破坏组增量发布、物理速度原地更新、共享快照、根动态体姿态回写缓存及官方临时分配器复用均已实现。

地形采用 4³ 碰撞叶、连续 64 槽嵌套 StaticCompound 缓存、局部旧/新范围唤醒、实际烘入缩放基准与官方接缝处理；塌方只标脏变化样本及梯度邻域。严格八角点盒形在成功构建 Hull 后可复用官方 BoxShape，保留实际圆角、密度、材质和偏移，其他形状继续使用 Hull；不保证浮点运动轨迹逐位一致。

当前保持单线程、碰撞精度和求解迭代数。本轮结束时，macOS 最终增量地形碰撞样本满足单次 <0.5 ms，Jolt 单步及整体物理未达标；首次建图/Reset、同帧重复提交、连通扫描和完整渲染上传仍有后续工作。后台任务、同帧损伤合并和其他模块优化未实施。统一的实现、可信数据与验证范围见[当前实现](../../runtime/performance.md#当前实现)和[最终可信基线](../../runtime/performance.md#最终可信基线)，不在多份文档重复维护逐轮数字。

## 实施顺序与验收

| 阶段 | 结果与验收重点 |
| --- | --- |
| 0. 技术验证 | 独立验证体积网格算法、Blast 基础子集与 Jolt 碰撞替换的编译和运行；记录 ARM 应力障碍，不提前接入引擎 |
| 1. 地形实验场 | 已实现同步小地图生成、跨块球形挖洞/填充、地表/洞壁颜色、碰撞更新和保存恢复；Debug/Release 定向测试通过，macOS Metal 窗口验收通过 |
| 2. 建筑破坏场 | 代码已实现官方参数盒体预破碎、连接损伤、Jolt 复合组与完整状态撤销，showcase 第七入口已加入；Mac 定向测试与 MCP 预览已通过，后续验收以步骤 2 验证记录为准 |
| 3. 综合联动场 | 同步首版已联通挖地基、建筑下落、撞击再破坏、烟尘和音频播放；Mac 定向测试及窗口流程通过，综合存档和其他平台联动尚未验收 |
| 4. 同步玩法补齐 | 已实现官方重力应力、动态土石、静态导航瓦片更新及有界火箭场；Mac 定向测试与窗口流程、Android 独立应力探针通过，完整移动宿主与实体手机性能未验收 |
| 5. 单线程性能优化 | 已完成分段地形碰撞、组树复用及物理准备/形状优化，macOS 实景结果见性能文档；本轮已停止优化，Jolt 单步与整体物理仍未达到 0.5 ms，异步和其他平台性能未验收 |

阶段探针与原始验证证据保留在忽略的 `build/` 和项目 `cache/`。功能场景统一在 `projects/showcase/` 内增加测试场景，复用原有角色与菜单，生成自制地形和建筑；同时维护桌面与 Android 运行资源清单，不新增游戏测试项目。分步安排与指标见[测试与性能计划](terrain-destruction-feasibility.md#首个项目内的分步实现与测量)。

综合场景的完整目标是第三人称角色在建筑旁挖洞、炸断地基、观察坍塌，并进入新洞穴、踩上残骸，保存后重开保持修改和碰撞状态。步骤 3 本轮已验证挖地基、下落撞击、二次损伤、表现、角色地面短走和重置；洞穴/残骸通行仍需专项验收，跨模块世界存档尚未实现。这些继续作为后续目标保留，不以本轮场景 Reset 代替存档。每阶段只做相关验证，不因研究选型触发全量依赖重编或自动 CI。

## 参考入口

- [Transvoxel 官方表与许可](https://github.com/EricLengyel/Transvoxel)
- [Voxel Tools 公开实现](https://github.com/Zylann/godot_voxel)
- [Blast 构建说明](https://github.com/NVIDIA-Omniverse/PhysX/blob/main/blast/README.md)
- [Jolt 官方仓库](https://github.com/jrouwe/JoltPhysics)
- [现有物理能力](../../runtime/physics.md)、[现有导航范围](../../runtime/navigation.md)、[插件边界](../plugins.md)
