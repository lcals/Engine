# 性能采样与对比流程

本文维护可复用的采样与分析流程、当前实现和性能基线。采用官方 Tracy 工具读取引擎埋点。最新可信基线为 2026-11-01 的 `terrain-step5-jolt-boxes`：本次地形增量碰撞更新样本低于 0.5 ms，但整个物理更新与 Jolt 单步仍未达到 0.5 ms。后续工作见[待优化项](#待优化项)。

## 开关与职责

- [PerformanceSystem](class-diagrams/performance-system.md) 已在运行宿主装配；引擎、插件与宿主在各自实现内提交采样，没有增加公开统计聚合接口。
- `ENGINE_ENABLE_TRACY` 默认 `OFF`。开启时目标私有链接 Tracy 0.14.1 on-demand 客户端；关闭时不查找、编译或链接该依赖，私有采样宏不求值。`Engine/System` 框架无 Tracy 依赖。
- 私有宏位于 `src/engine/runtime/performance/profiling.hpp`，由 `cmake/core/profiling.cmake` 对明确目标启用。`ENGINE_PROFILE_ZONE` 的名称必须是静态字符串；私有 Jolt job 观察器另用复制名称的 transient zone。正常 `bgfx::frame()` 后提交唯一默认 FrameMark。
- bgfx 状态面板仍默认关闭，通过已有 MCP 开关控制；它与 Tracy 构建开关独立。on-demand 客户端即使未连接也有线程和监听成本，不等于完全零开销。
- 当前覆盖宿主工作/限帧等待、脚本、UI、动画、音频主线程管理、特效、地形、破坏、物理、导航、渲染和 oneTBB 任务域。没有覆盖 SDL 后台混音或全局内存分配调用栈。

## 固定操作步骤

1. **确定比较条件。** 记录代码提交、实际可执行文件指纹、资源 revision、项目/场景、设备/系统、后端、分辨率、构建类型、脚本后端、帧率限制、镜头、随机种子和操作序列。每轮从同一明确状态开始；重置在采集窗口外完成。
2. **准备工具和二进制。** 只在需要时增量构建 Release + Tracy；已有依赖、工具和可执行文件可复用。Debug 用于正确性检查，不用其帧耗时代表发布性能。保持 QuickJS，不因采样重编 V8，也不运行 CI。
3. **启动并核对目标。** 新编辑器实例先等日志出现 `Editor ready`，再首次 `sync_assets`；核对 active/available revision 后启动项目，确认 MCP 回执中的 project、runtimeDir、资源版本及活动场景。提前同步可能被视为上次会话已有版本而未激活，不能用重复发布同版本代替激活确认。进程启动身份只保留在本地采集 metadata 中。采集期间不并行编译、截图、导入、跑测试或做离线分析。
4. **连接后再操作。** 先采集静止基线，再按固定序列施加负载。等待包装器的 `connected` 事件后，才通过已有项目 UI/MCP 开始负载；输入操作独立保留回执。采集器不点击按钮、不重放爆炸，也不重试业务请求。
5. **保存原始证据。** 保留 `.tracy`、官方采集日志、采集 metadata/RSS、操作回执及完成消息；输出放 `build/performance/<run>/`，不提交原始 trace、巨大 CSV、缓存或本机路径。失败输出不覆盖，重新采样使用新目录。
6. **离线分析同一窗口。** 用业务首条/完成消息圈选活动段，扩展到所属完整 `Host.frame`；也可明确传入纳秒边界。排除重置/菜单/未完成首末帧，不删除活动窗口里的慢帧。报告帧均值、P50/P95/P99/max、实际吞吐及最终对象数量。
7. **定位而后改动。** 在当前开发模块及其直接链路内看 self 热点，再展开最慢帧的 inclusive 调用链；交叉查看 GPU、等待和内存。其他模块热点只记录，不因耗时更高而扩大范围。把实测区间、根据源码推导的原因以及尚未验证的假设分开。一次处理一个主要原因，保留同口径优化前/后结果。
8. **清理并说明范围。** 停止项目，确认场景清空、脚本停止且无错误；仅关闭本次拥有的引擎进程。记录采样开关、测试范围和未验证的平台，不把模拟器或 Mac 结果写成手机性能。

所有新一轮性能工作先使用本流程及现有工具，不重新在临时目录写一套分析器。场景专用操作继续放项目脚本或既有 MCP 调用方，引擎埋点不识别特定场景。

## 工具使用

仓库工具位于 [scripts/performance](../../scripts/performance/)。分析器接收已有 trace，不启动引擎；采集包装器只创建官方 capture 子进程，可选只读同机 RSS，不终止目标进程。

引擎通过根 CMake Presets 构建，在需要采样的现有 Release 配置追加 `-DENGINE_ENABLE_TRACY=ON`。例如已准备依赖的 macOS 原生配置：

```sh
cmake --preset macos-native-release -DENGINE_ENABLE_TRACY=ON
cmake --build --preset macos-native-release --target editor_app engine_app mcp_app
```

其他平台使用对应根预设；环境与依赖准备见根 [README](../../README.md)。采样结束后的正式构建重新配置为 `-DENGINE_ENABLE_TRACY=OFF` 并增量构建，不为切换开关删除依赖缓存。

工具客户端与服务器使用同一 Tracy 版本。当前为 0.14.1，复用 vcpkg 已准备的上游源码。独立工具入口只构建官方 capture/csvexport 及一个使用官方 Worker API 导出 FrameMark 的小程序，不包含引擎、V8 或分析器 GUI。配置时提供本机已有的工具依赖前缀；工具依赖缺失时按 `scripts/performance/vcpkg.json` 的独立开发工具清单准备，不清理或重建整套引擎。使用两个工具构建目录，保留官方 capture 的 `NO_STATISTICS`，避免采集期间额外计算离线统计。宿主仍要求 Ninja 和 ccache（Windows 为 sccache）。

```sh
cmake -S scripts/performance -B build/performance-tools/capture -G Ninja \
  -DPERFORMANCE_TOOL=capture \
  -DTRACY_SOURCE_DIR="<tracy-0.14.1-source>" \
  -DCMAKE_PREFIX_PATH="<host-tool-dependencies-prefix>" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/performance-tools/capture --target tracy-capture
cmake -S scripts/performance -B build/performance-tools/analyze -G Ninja \
  -DPERFORMANCE_TOOL=analyze \
  -DTRACY_SOURCE_DIR="<tracy-0.14.1-source>" \
  -DCMAKE_PREFIX_PATH="<host-tool-dependencies-prefix>" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/performance-tools/analyze --target tracy-csvexport tracy-frameexport
```

先把本轮条件写入忽略目录中的 `metadata.json`，例如：

```json
{
  "code_commit": "<commit>",
  "binary_sha256": "<executable-sha256>",
  "device": "<CPU/GPU/RAM/OS>",
  "configuration": "Release",
  "script_backend": "quickjs",
  "renderer": "Metal",
  "resolution": [2560, 1440],
  "target_frame_rate": -1,
  "project": "projects/showcase",
  "scene": "Combined destruction laboratory",
  "resource_revision": "<published-revision>",
  "camera": "Overview",
  "scenario": "Reset 后 60 发，6 发/秒，等待在途弹体清空"
}
```

macOS/Linux 采集示例，`--pid` 为已核对的同机引擎 PID，不能用远端设备 PID：

```sh
python3 scripts/performance/capture.py \
  --capture build/performance-tools/capture/capture/tracy-capture \
  --output build/performance/before-6 \
  --metadata build/performance/metadata.json \
  --pid <engine-pid> --seconds 35
```

包装器通过 PTY 确认官方连接消息，避免 stdout 缓冲造成“未连接”的误判；连接超时不执行任何场景操作。RSS 默认约 0.5 秒采一次，并校验 PID 的启动身份。远端客户端可指定 `--address`、`--port`，省略 `--pid`；设备内存另外采集。Windows 可直接用同版本官方 capture/GUI 保存 trace，再使用相同离线流程，当前包装器不宣称支持 Windows 进程采样。

```sh
python3 scripts/performance/summarize_trace.py \
  --trace build/performance/before-6/capture.tracy \
  --csvexport build/performance-tools/analyze/csvexport/tracy-csvexport \
  --frameexport build/performance-tools/analyze/tracy-frameexport \
  --output-dir build/performance/before-6/analysis \
  --message-prefix "Rocket range sample " --start-field fired --start-value 1 \
  --complete-field complete --frame-zone Host.frame
```

空闲场景省略消息参数，使用所有完整 FrameMark 间隔；其他业务可传自己的 JSON 消息前缀与完成字段，或者 `--start-ns`/`--end-ns`。一个 trace 只放一轮负载，避免把多次运行误合并。输出包含官方逐事件导出、帧间隔和 `summary.json`；原始输入与分析配置必须一同保留。

地形碰撞预算继续使用已有分析目录中的 `summary.json` 和 `zones.tsv`，保持相同窗口，运行离线汇总：

```sh
python3 scripts/performance/summarize_terrain_collision.py \
  --analysis-dir build/performance/before-6/analysis \
  --output build/performance/before-6/analysis/collision-budget.json
```

输出必须是 `build/` 下的新文件，已有结果不会覆盖。每次 `Terrain.commit` 的完整碰撞成本累计内部 `Terrain.prepare_collision`、`Physics.set_mesh_parts`（兼容旧 `Physics.set_mesh`）以及 `Terrain.release_previous_volume`；最后一项包含旧几何最后引用的释放，并保守包含旧密度释放。`Terrain.collision_compare` 已包含在准备时间内，不重复相加。外部 `Terrain.fragment_mesh` 的准备时间单列。JSON 保留每次提交、按完整 `Host.frame` 累计的结果、mean/P95/max 与 ≥0.5 ms 次数；P95 沿用线性插值。最低预算为每次提交严格低于 0.5 ms。

缺少 `Terrain.release_previous_volume` 埋点的旧 trace 只累计已测项，同时在 `budget_coverage` 列出缺项提交；其 `release_previous_volume_ms` 为 `null`，未超预算时 `meets_budget` 也为 `null`，不能据此认定完整成本达标。已测部分超预算仍明确失败。完整 `Terrain.commit` 另行保留逐次及总体耗时，不隐藏粗块网格生成、单元区间记录等其余成本。

有变化/无变化分组以提交内部 `Physics.create_mesh_compound` 或 `Physics.create_shape` 事件作为形状重建依据；这是埋点代理，不能代替逐三角几何比较。粗块重建数量来自提交内部 `Terrain.mesh_chunk` 次数，32 粗块全量提交特别标记并继续计入总体，不当作异常值删除。脚本流式读取 TSV、按线程和 zone 建索引；plot、缺少时间的行及窗口外不完整区间不会混入碰撞预算。

优化前/后使用独立输出目录，逐项对比：帧 P95/P99/max、实际吞吐、相同对象数量下的主线程工作量、主要 zone self/inclusive、GPU 分布、RSS 初末峰值。资源/镜头/分辨率/工作量改变时另列为不同条件，不能直接宣称加速比例。短样本用于定位明显问题，不能替代持续热负载、泄漏或手机验收。

另有 `engine_terrain_collision_benchmark` 可执行目标，使用自制几何测量 CPU 接口提交，可用 `--chunk-cells`、`--repeats`、`--mode parts|legacy` 和 `--tick` 控制比较。`legacy` 仅是单网格接口的对照模式。此探针的 JSON 使用 `nearest_rank` P95，`over_0_5_ms` 统计严格大于 0.5 ms 的次数；与实景的线性插值和 ≥0.5 ms 失败计数不同，不作为严格预算验收，也不混入实景基线。

## 统计口径

| 数据 | 含义与限制 |
| --- | --- |
| FrameMark 间隔 | 实际相邻完整帧的墙钟时间；排除 on-demand 连接前及断开末尾的不完整区间 |
| CPU zone | 当前线程经过的墙钟时间，可能包含等待；不是 CPU cycles、CPU 利用率或 GPU pass 耗时 |
| inclusive / self | inclusive 含已埋点子调用，父子不可直接相加；self 仅扣已埋点同线程子区间，仍含未细分计算与等待 |
| Host work | 显式限帧等待前的工作时间，仍包含渲染提交中可能发生的等待 |
| bgfx frameInterval | 官方 `cpuTimeFrame`，包含业务/限帧，不能称为渲染算法耗时 |
| bgfx GPU / submit | GPU 使用独立时钟且可能滞后；submit 是渲染线程区间。frame number 不可靠时只比较分布，不强行配对 CPU 最差帧 |
| RSS | 操作系统周期采样的进程驻留内存；可能漏瞬时尖峰，不是 allocation/free 记录或泄漏证明 |
| bgfx 内存 | texture/RT 是资源估算，驱动 resident/budget 仅在有效时记录；存在口径重叠，不能简单相加或再加到 RSS |

百分位采用排序后的线性插值。plot 平均值为记录样本的平均，不是按时间加权。不要使用官方 csvexport 的 `total_perc` 作为 on-demand 窗口占比；分母应是明确的活动窗口或同窗口 Host work。官方 0.14.1 self 导出中的 `std_ns` 不用于本流程：其平方和与 self 累计口径不一致；本地从原始事件重新计算统计值。

Jolt job 释放依赖时可能同步执行嵌套 job，阶段分布必须使用 `self_ms`，不能把各 job 的 inclusive 相加。`Physics.collect_impact` 是 FindCollisions 的子范围，`Physics.estimate_impact` 又包含在 collect 内。对账使用“全部 job self + collect inclusive + Jolt 外层 self”，不再加 estimate。job 观察器自身有计时和包装成本，与未启用该观察器的旧 trace 比较时必须注明。

同帧物理累计按线程将完整 zone 归入包含它的 `Host.frame`，对同名区间取并集，另报未归帧事件；同时报告全部完整帧和有步进帧，保留零步与多步帧。单步 ≥0.5 ms 次数与同帧累计 ≥0.5 ms 帧数是不同指标。`Physics.tick` 已包含 prepare、固定步和结果同步，不能把它们相加成总耗时。

## 当前实现

当前保持单线程同步物理、原碰撞精度、求解迭代数及项目炮击参数。以下能力已经落地：

- **场景与破坏发布。** `Scene` 使用运行时/局部 ID 索引和线性子树删除；破坏插件按组内 chunks 与 anchored 状态复用未变节点、网格、碰撞与刚体，只发布实际变化的组。血量、冲量和速度变化仍按各自契约处理。
- **物理准备与写回。** 碰撞描述使用共享不可变快照，完整比较描述和 Mesh 引用后才复用校验；复用快照数组容量及帧标记。仅速度变化使用官方原地更新。根动态体记录最后成功写回的精确 Transform 和原始 Jolt 姿态，避免 Euler 往返触发伪姿态提交，业务姿态/缩放变化仍正常生效。静态形状替换不重复提交未变属性，速度合并读取。
- **临时内存。** 复用官方 `TempAllocatorImplWithMallocFallback` 预留的 10 MiB 栈式分配缓冲区，超出容量使用官方 malloc 回退。
- **分段地形碰撞。** 保留一个地形 Node/Collider/Body，碰撞内部使用 4³ 单元叶子；核心按连续 64 槽缓存官方复合树分组，只重建变化叶、组与根。复用 Jolt `MeshShape`、`FavorBuildSpeed`、嵌套 `StaticCompoundShape` 与 Enhanced Internal Edge Removal。渲染粗块大小不变，不简化碰撞三角形；形状缩放以实际烘入值为比较基准。
- **地形与土块准备。** 塌方只标脏抽走样本及梯度邻域，法线保持与全量重建一致。土块碰撞仅扫描负密度包围盒及相邻单元，跳过纯空气，并使用固定栈数组暂存角点与交点；保留凹形轮廓和边界。
- **严格盒形识别。** 完整且精确的八角点长方体先成功创建官方 Hull，再替换为保留其实际圆角、密度、材质和局部偏移的官方 BoxShape。缺角、不规则或微斜顶继续使用 Hull，失败沿用原结果；浮点质量计算、接触选择和运动轨迹不保证逐位相同。
- **Jolt 阶段采样。** Tracy 开启且已连接时，私有观察器装饰官方 JobFunction；实际 Job、Barrier、依赖与执行仍归官方 `JobSystemSingleThreaded`。关闭 Tracy 使用官方类型，无需改动 Jolt ABI 或重编上游启用内部 profiler。

架构与契约见[场景](class-diagrams/scene.md)、[物理](class-diagrams/physics-system.md)、[地形](../plugins/runtime/class-diagrams/terrain.md)、[破坏](../plugins/runtime/class-diagrams/destruction.md)及[地形碰撞方案](../plugins/runtime/terrain-collision-plan.md)。

## 最终可信基线

2026-11-01，Apple M1 Max / 64 GiB / macOS ARM64，Metal 2560×1440，Release / QuickJS / Tracy 0.14.1 on-demand，资源版本 `521195c3c186be37`。项目为 `projects/showcase`，场景为 Combined destruction laboratory，固定 Overview 镜头。Reset 后请求 12 发/秒、共 60 发；重置与首次建图在活动窗口外，采样时没有并行构建、测试、截图或调用栈采样。

原始 trace 为 35 秒；统计窗口从首条 `fired=1` 到 `complete=true`，扩展到完整 `Host.frame`，共 10.175529 秒、1176 个完整 Host.frame 和 1175 个完整 FrameMark 间隔。实际射速 11.201823 发/秒，41 命中、19 未中、无弹池阻塞，最终 434 个建筑组（433 个动态组）、67 土块；物理 body 末值 503，活动 body 末值 418。射速按首发至末发计算，活动窗口还包含在途弹体清空时间。

| 指标 | 次数 | mean ms | P95 ms | max ms | ≥0.5 ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| 完整单次地形碰撞更新 | 42 | 0.195545 | 0.421644 | 0.468999 | 0 |
| 有提交帧的碰撞累计 | 39 帧 | 0.210587 | 0.426331 | 0.468999 | 0 帧 |
| Physics.prepare | 1176 | 0.240509 | 0.300073 | 0.407208 | 0 |
| Physics.sync_results | 1176 | 0.079984 | 0.150927 | 0.212167 | 0 |
| Physics.jolt_step | 610 | 1.135225 | 1.976267 | 3.280668 | 454 |
| Physics.tick | 1176 | 0.918413 | 2.301606 | 9.183753 | 557 |
| 同帧 Jolt 累计（含零步帧） | 1176 帧 | 0.588850 | 1.868772 | 8.733795 | 444 帧 |
| FindCollisions self | 567 | 0.826787 | 1.507801 | 2.334334 | 337 |

地形预算完整覆盖叶子准备、同步物理提交和旧体积释放；26 次有形状构建、16 次没有形状构建，窗口内没有 32 粗块全量提交。**本次增量碰撞样本满足每次 <0.5 ms；整个物理更新和 Jolt 单步仍未达到 0.5 ms。** 不能由本样本推断首次建图、Reset 或任意后续同帧提交都达标。完整 `Terrain.commit` 的 mean/P95/max 为 1.621685/2.726253/2.936834 ms，其中还包括网格和渲染准备，不能整体称为碰撞更新。

固定步分布为：576 帧零步、595 帧一步、3 帧两步、1 帧三步、1 帧六步。600 个有 Jolt 步进的帧累计 mean/P95/max 为 1.154146/1.977017/8.733795 ms。追赶帧必须保留，不能用单步均值代表整帧物理耗时。

FindCollisions self 累计 468.788229 ms，占 Jolt 步进总计 692.487364 ms 的 67.70%；速度约束与位置约束 self 分别累计 61.519226/45.703263 ms。全部 job self 614.185113 ms，加接触回调 inclusive 67.778588 ms、Jolt 外层 self 10.523663 ms，与步进总计一致。FindCollisions 同时含空间树查询、缓存与窄相，尚不能把其全部成本归于某一个子算法。

炮击窗口记录 20 次成功 `Physics.box_hull` 替换；这是构建事件数，不是全场盒形物体占比，初始场景构建在窗口外。该 zone 只覆盖成功替换赋值，不代表识别及完整形状构建成本。

整场帧间隔 mean/P50/P95/P99/max 为 8.651891/8.336962/10.046859/17.876716/117.301716 ms。GPU mean/P95 为 5.759959/9.900500 ms，仅按分布报告；此样本 GPU frame number 为 0，不能与 CPU 最慢帧强行配对。完整 35 秒采集的进程 RSS 首/末/峰为 303.56/367.53/372.03 MiB，包含 Tracy 和进程其他模块，不能作为物理单项内存或泄漏结论。

仅保留一个历史对照，全部为相同场景配置下的独立 12 档、60 发观测：

| 采样目录 | Jolt 单步 mean/P95/max ms | 命中/未中 | 最终动态组 |
| --- | --- | ---: | ---: |
| terrain-step5-parts-verified | 1.337663 / 2.228616 / 3.106961 | 43 / 17 | 393 |
| terrain-step5-jolt-optimized | 1.344358 / 2.230110 / 2.920294 | 44 / 16 | 427 |
| terrain-step5-jolt-boxes | 1.135225 / 1.976267 / 3.280668 | 41 / 19 | 433 |

命中、接触数量与轨迹不同，verified 还缺少新增 job 计时开销；均值和 P95 较低不能写成严格同负载加速比，最终单步最大值也没有下降。中间诊断和全部原始结果保留在本地证据中，不作为当前实现说明。

## 待优化项

当前结束本轮优化，以下均为未完成工作，后续由选定模块和实测目标决定是否开展。

1. **物理窄相与接触成本。** FindCollisions 仍占 Jolt 约 67.70%。后续先固定形状和运动状态，再区分空间树、接触缓存及具体形状对的窄相成本；必要的短调用栈采样独立进行，不混入验收 trace。保留冲量估算和碰撞精度。官方接触缓存仍须在地形根形状变化时正确失效，叶子共享不能替代失效契约。
2. **物理准备与追赶帧。** prepare 和结果同步仍各消耗每帧约 0.241/0.080 ms；同帧步进最多累计 8.734 ms。后续比较必须同时报告单步与同帧累计，保留同步查询即时生效和外部姿态修改契约，不能通过少算固定步或减迭代隐藏成本。
3. **全量地形与重复提交。** 最终炮击窗口未覆盖首次建图或 Reset。已有本地 CPU 探针的首次全量碰撞接口提交为 4.306833 ms，仅包含接口提交，尚不满足 0.5 ms；它不是本次实景完整预算。同帧重复地形/损伤提交合并尚未实现，需要先明确即时查询与失败原子性边界，再做实测。
4. **地形连通扫描与塌方事务。** 最终 `Terrain.collapse` 最大 20.893509 ms，完整 `Terrain.commit` 最大 2.936834 ms，仍为同步成本。有预算的后台准备尚未实现；若后续采用，需独占快照、版本校验、过期取消和安全时点发布，不能把含场景增删的整个事务直接移到后台。
5. **验证覆盖。** 后续需要长期热负载、更多碰撞形状和重复 Reset 的内存观测；当前数据仅覆盖 macOS，不构成 Windows、Android 或 iOS 性能验收。

其他模块只保留定位记录：最终最慢帧包含 `Audio.tick` 106.033753 ms，原因尚未细分；`Ui.synchronize` 共 204172 次、self 累计 2409.674901 ms；`Navigation.replace` 最大 11.418880 ms。UI dirty 更新及上述音频、导航热点均未处理，跨模块优化需单独选定目标。

## 验证与证据

最终 Mac Debug（Tracy OFF）/ Release（ON）的编辑器、运行宿主和受影响目标构建通过；两配置的 `engine.physics`、`engine.character_controller`、`engine.terrain_runtime`、`engine.destruction_runtime`、`editor.destruction_commands` 各五项 CTest 通过，耗时分别为 5.48/3.09 秒。回归覆盖分段发布失败原子性、空叶/缩放/父变换、跨缝与旧支撑唤醒、塌方法线一致性、零步姿态稳定性、外部修改，以及严格盒的偏移/缩放/间隙和缺角/微斜顶保真。未执行全仓测试、自动 CI 或其他平台性能采样。

本地证据统一位于 `build/terrain-step5-jolt-boxes/`，不提交原始 trace、逐事件导出或本机运行目录：

- `barrage12/capture.tracy`、`capture.json`、`capture.log`：原始采样、metadata、二进制指纹和 RSS。
- `barrage12-metadata.json`、`barrage12-report.json`、`barrage12-receipts.jsonl`：场景条件、实际负载与操作回执。
- `analysis12/summary.json`、`zones.tsv`、`self.tsv`、`frames.csv`、`messages.tsv`、`collision-budget.json`：同窗口统计、完整预算和官方逐事件导出。
- `physics-comparison.json`：四轮物理比较，包含阶段 ≥0.5 ms 次数、同帧累计、固定步分布、盒形命中数与最终 job self 分布；这是已保存结果，通用重采样入口仍是上文三个脚本。
- `build-debug.log`、`build-release.log`、`tests-debug.log`、`tests-release.log`：实际构建与定向验证记录。
- `cleanup-report.json`、`cleanup-receipts.jsonl`：`stop_project` 后场景为空、节点为 0、脚本停止且无错误，并已确认本次引擎进程退出。

## 任务线程观测与迁移次序

[线程与任务类图](class-diagrams/task-system.md)维护公共契约。Task.execute 的单行注释保存 domain、task、priority 和 queued_ns；排队纳秒从提交算到函数开始，批次节点也包含依赖等待。Task.batch 包含图构造、协作执行和等待，不能将其 inclusive 与同线程子任务耗时相加。Task.wait / Task.shutdown 表示域等待及关闭成本，不能当作任务的 CPU 运算时间。后台线程继续复用现有 Tracy 客户端。

调度底座本身不改变场景帧耗时。后续按下表拆分，每次只迁移一条实际调用链并用本文流程采集前后同条件证据：

| 次序 | 对象与复用方案 | 留在协调线程的部分与验收重点 |
| --- | --- | --- |
| 1 | 单项资源读取、解码准备放运行时域；编辑器转换已使用独立域 | 先建立不可变目录/挂载快照及结果代际校验，再让主线程按预算接收。现有 ResourceSystem/ResourceGraph 的活动缓存和目录不能直接并发读写；退出或换场景后的旧结果丢弃。加载等待、磁盘吞吐、主线程最大停顿分别统计。 |
| 2 | 地形网格/碰撞候选、导航脏瓦片等 CPU 准备，复用现有算法与批次依赖 | 工作线程只读快照、产生候选；主线程提交 Jolt shape、导航瓦片与场景对象。先确认候选所有权、版本号和单帧提交预算，不直接把整个系统 tick 挪走。 |
| 3 | Jolt job 图和动画批次，优先使用官方 job/并行接口 | 物理世界更新边界、接触事件归并、角色输入/相机顺序保持明确。当前 TaskSystem 为协调线程提交接口；Jolt 内部动态 job 提交需要按其官方契约单独适配评审，不能从 job 回调阻塞等待本池 future。动画只并行独立角色的采样/姿态准备，主线程提交最终状态。 |
| 4 | 渲染 CPU 准备及 bgfx 已有内部渲染线程 | 先分别采集场景准备、提交、bgfx 等待及 GPU；需要并行时使用 bgfx 官方 encoder/线程契约，不在外面再套一个碰 GPU 对象的通用线程。 |
| 5 | 音频资源准备 | SDL 音频输出已有后台线程；场景声音状态和回调数据必须交接。混音回调不等待普通任务、不调用场景或 VM，不重复建第二套混音线程。 |

输入、UI 事件和主线程状态应用继续优先推进；高优先级不抢占已开始的任务。除了任务/arena 优先级，还必须控制后台并发、长任务粒度与主线程接收预算。当前只完成底座和已有导入转换迁移，上述消费者的后台化不算本轮交付。

2026-11-02，macOS ARM64 / Release / Tracy ON：两个任务域的独立埋点探针完成 120 帧、120 个依赖批次和 360 次用户函数，正式 capture → csvexport → summarize_trace 流程通过；任务分别在三个 worker 上执行，主线程没有执行用户函数，任务名/域/优先度/排队注释可读。该探针包含合成等待，仅验证埋点与线程归属，不是游戏性能基线，也不替代上面的物理样本。原始证据保留于本地 build/performance/threading-smoke，未进入版本管理。
