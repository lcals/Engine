# 性能采样

本页只维护可复用的工具、口径和已知边界。性能采样由明确的性能任务触发；默认一个场景、一对前后结果，出现噪声或结论不足时才增加轮次。当前渲染数据见[性能基线](../testing.md#渲染性能基线)，不在多份文档重复维护。

## 开关与职责

- `ENGINE_ENABLE_TRACY` 默认 OFF；开启时目标私有链接同版本 Tracy on-demand 客户端，公开核心接口不依赖 Tracy。
- 埋点位于模块实现，私有宏见 `src/engine/runtime/performance/profiling.hpp`。关闭时不查找、编译或链接 Tracy。
- Render.frame 包含提交与等待；GPU 工作时间和完成帧另取 bgfx Stats。CPU future 完成、CPU 帧末与 GPU 完成不是同一状态。
- bgfx 状态面板由已有 MCP 开关控制，与 Tracy 构建开关独立，默认关闭。

## 固定操作步骤

1. 固定模块、场景、对象数量、资源版本、镜头、操作序列、分辨率、效果、VSync 与帧率限制；使用同一实际设备和 Release 配置。
2. 保存代码版本、二进制指纹、编译开关、设备/驱动和采样参数。初始化与资源准备单列，不混入稳态渲染。
3. 一个 trace 只采一轮明确负载。计时期间不并行构建、导入、截图、测试或离线分析；慢帧保留，不事后删异常值制造收益。
4. 对同一窗口查看 self / inclusive、主线程工作与等待、GPU、实际工作量和内存。优先处理当前模块的热点，其他模块只记录。
5. 固定负载比较一对结果；只在波动、失败或未解释退化时追加。没有分阶段采样时，只报告整体耗时，不能宣称具体函数的加速比例。
6. 原始产物放忽略的 `build/performance/<run>/`，失败结果不覆盖。结束时正常停止本次项目和进程；当前摘要更新到[测试文档](../testing.md)，不追加会话流水账。

## 工具使用

仓库工具位于 [scripts/performance](../../scripts/performance/)。分析器接收已有 trace，不启动引擎；采集包装器只创建官方 capture 子进程，可选只读同机 RSS，不终止目标进程。

引擎通过根 CMake Presets 构建，在需要采样的现有 Release 配置追加 `-DENGINE_ENABLE_TRACY=ON`。例如已准备依赖的 macOS 原生配置：

```sh
cmake --preset macos-native-release -DENGINE_ENABLE_TRACY=ON
cmake --build --preset macos-native-release --target editor_app engine_app mcp_app
```

其他平台使用对应根预设；环境与依赖准备见[构建指南](../building.md)。采样结束后的正式构建重新配置为 `-DENGINE_ENABLE_TRACY=OFF` 并增量构建，不为切换开关删除依赖缓存。

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

采集 metadata 至少记录提交、二进制 SHA256、设备/驱动、后端、Release/Tracy 开关、项目/场景、资源版本及操作参数；PID 和本机绝对路径只保留在本地产物。

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

空闲场景省略消息参数，使用所有完整 FrameMark 间隔；其他业务可传自己的 JSON 消息前缀与完成字段，或者 `--start-ns`/`--end-ns`。一个 trace 只放一轮负载，避免把多次运行误合并。输出包含官方逐事件导出、帧间隔和 `summary.json`；原始输入与分析配置必须一同保留。`task_queue` 按域、任务和优先级汇总单行注释中的 `queued_ns`，分别输出排队与执行耗时、执行线程及缺失/无效注释数量；旧 trace 缺注释时不虚构排队数据。

## 统计口径

- 帧报告均值、P50/P95/P99/max、样本数及实际吞吐；说明分位数算法，保持两版一致。
- CPU 区间区分自身工作、子区间与等待，父子 inclusive 不相加；CPU/GPU 异步数据不能直接相减定位热点。
- RenderViewStats 是场景网格统计；阴影、后处理等总提交另取 bgfx Stats。合批可以减少 draw，不能减少应绘制实例或三角形。
- GPU 时间保留有效性与 `gpuFrameNum`；没有可用时间时写“不可用”，不以 CPU 墙钟替代。
- 进程 RSS、CPU 暂存、纹理与 RenderTarget 估算分别报告，不能混称显存或内存收益；未测的项明确留空。
- 图像、功能与生命周期先满足契约；不能以降画质、少绘制、少算物理步或放宽容差换取性能结论。
- 场景和参数不同只能分别报告，不能算严格加速比例。短样本不代表持续热负载、完整首帧或移动设备性能。

地形专项沿用 `summarize_terrain_collision.py --analysis-dir <analysis> --output <new-file>`，不再另写分析器。同步提交、异步后台准备与主线程发布分开；缺少请求关联或完整埋点时报告未知，不按相邻时间猜测一次请求的总延迟。

## 并行观测

线程契约见[任务类图](class-diagrams/task-system.md)，项目额度见[并行预算](parallelism.md)。Task.execute 的注释包含域、任务、优先度和排队时间；Task.wait / shutdown 表示等待，不是任务 CPU 运算。

| 链路 | 需要区分的工作 |
| --- | --- |
| 文件与资源 | I/O、单项解码、依赖调度、结果发布、在途数量和字节 |
| 渲染 | 纹理准备/上传、帧收集、视图列表、组批、提交等待、GPU 与读回 |
| 物理与导航 | worker 工作、必要的本帧 barrier、主线程发布与同帧累计步数 |
| 动画 | 实例采样、join、结果发布；首次资源准备单列 |
| 地形与破坏 | 后台候选、主线程发布、同步连通扫描/塌方和物理更新 |
| 音频 | 参数发布、SDL 供数、初始化与设备生命周期；回调不能访问场景或 VM |

资源准备的项目数、时间和暂存预算不覆盖驱动队列、UI/字体或其他插件的全部开销。主线程仍处理平台事件、UI/VM、结果发布与必要的 join；并行不等于所有路径已经跨帧异步。

## 采样结果

当前 Windows 渲染基线和代价统一见[测试记录](../testing.md#渲染性能基线)。Shader/PSO 预热、CPU 子阶段占比及整体启动速度尚未验证。

2026-11-01 的 macOS 单线程地形/物理样本、后续并行样本不再作为当前版本的性能依据；历史过程保留在 Git 与本地 `build/terrain-step5-jolt-boxes/`。当时 Jolt 单步与整体物理没有达到 0.5 ms，不能把局部碰撞更新的结果外推为整条链路达标，也不能将这些数字外推到 Windows 或手机。
