# 运行并行与项目预算

[运行设置](class-diagrams/runtime-settings.md) · [运行宿主类图](class-diagrams/runtime-host.md) · [线程与任务](class-diagrams/task-system.md) · [资源加载](class-diagrams/resource-loader.md) · [玩法会话](class-diagrams/gameplay-session.md)

桌面和移动端共用同一套并行实现。公共与平台预算完整保存在项目 TOML 中，脚本不管理线程、线程池或跨线程场景状态。主线程继续负责平台事件、当前 UI/VM、场景生命周期和结果发布。

## 配置来源

项目按模块保存 `settings/runtime-game.toml`、`runtime-engine.toml`、`runtime-parallel.toml`。编辑器读取源配置；独立 Python 打包脚本收集所有 `runtime-*.toml`，只合并 `[common]` 与 `[platform.<当前目标>]`，最终输出按模块组织的一份 `settings/runtime.json`。运行时不读取 TOML，不依赖项目目录。

下列片段展示公共配置与平台覆盖的关系，完整参数见公开范例的 [runtime-parallel.toml](../../projects/showcase/settings/runtime-parallel.toml)。`runtime-engine.toml`：

```toml
[common]
window_title = "Engine"
width = 1600
height = 900
target_frame_rate = 45

[platform.android]
target_frame_rate = 30
```

`runtime-parallel.toml`：

```toml
[common.threads]
worker_priority = "low"

[platform.android.threads]
worker_limit = 2
```

Android 打包结果包含以下字段（其余必填项从完整 TOML 收集）：

```json
{
  "engine": {"width": 1600, "target_frame_rate": 30},
  "parallel": {"threads": {"worker_priority": "low", "worker_limit": 2}}
}
```

游戏默认语言在 `runtime-game.toml` 的 `[common].game_language` 配置，汇总为 JSON 的 `game.game_language`。宿主读取后交给 `App`，TS 使用只读 `App.gameLanguage`；公开范例显式填写 `en`，业务自行决定如何使用。

项目文件完整保存参数，平台字段覆盖同名公共字段；配置读取链路不提供参数默认值，不复制默认值表。三个运行模块及当前支持的各字段均必填，缺少配置时明确失败。宿主分别向 App、RuntimeHost 和其他模块提交参数；CLI 的窗口尺寸及 worker 覆盖最后应用。RuntimeHost 调用者必须显式提交并行预算。

品质分组统一约定为 `[quality.<档位>]`，由业务选择；目前项目未使用品质配置，本轮不添加品质自动选择或切换功能。填写非空品质表会明确报错，避免把未消费的参数静默丢掉。

源配置与运行 JSON 接收上限均为 64 KiB。运行时拒绝未知模块、错误字段类型与非法范围，失败不改变调用方已有值。打包负责 TOML 结构检查、模块归组和目标平台筛选；字段语义由共用 C++ 解析器检查。实际输出可传给 `engine_runtime_settings_test` 验证，编辑器通过同一解析器校验源配置。独立打包当前尚未在发布前调用完整原生字段校验，非法字段会在运行宿主启动时失败；不在 Python 重写整套参数规则。

## 可调参数

| 字段 | 语义与约束 |
| --- | --- |
| `threads.worker_limit` | oneTBB worker 预算；0 自动，显式 1–64。它不包含 SDL 音频、I/O 和 bgfx 自有线程 |
| `threads.worker_priority` | worker 的操作系统调度提示：low / normal / high |
| `runtime/background/loading/physics/animation/navigation.max_concurrency` | 各任务域并发额度；0 随总预算，显式 1–64，不是独占线程 |
| 各任务域 `priority` / `max_pending` | 非抢占任务优先级与未完成/未派发结果积压上限；积压 1–65536 |
| `resources.max_in_flight` | 已开始读取/解码的资源候选数量，1–1024 |
| `resources.max_in_flight_bytes` | 候选原始实体字节预算，1–4 GiB；不代表解码后对象或总进程内存上限 |
| `resources.completion_budget` | 一次资源轮询最多派发的文件完成事件数，1–65536 |
| `animation_sampling.min_parallel_count` / `batch_size` | 动画并行阈值与批大小，1–1000000 |
| `navigation_tile_threshold` | 导航 tile 构建并行阈值，1–1024 |
| `scene_restore.nodes_per_step` / `max_seconds` | 主线程场景恢复批量与时间预算；节点 1–1000000，秒数 (0, 1] |
| `render.texture_batch_size` / `texture_staging_bytes` | 贴图准备批量与暂存字节预算；批量 1–64，字节 1–4 GiB |
| `render.prepare_items_per_step` / `prepare_seconds_per_step` | 分步准备的数量与时间软预算，默认 64 项 / 0.002 秒；均须大于零，时间须有限 |
| `render.upload_bytes_per_step` | 每步纹理上传字节预算，默认 8 MiB；大纹理跨帧上传，单个不可拆解码单元超出暂存预算则失败 |
| `completions_per_frame` / `completion_budget_seconds` | 主线程完成委托派发数量与总时间预算；数量 1–65536，秒数 (0, 1] |

配置在启动时读取。本轮不自动重读文件，不承诺运行中任意缩减 worker 或域额度。时间预算在工作项之间检查，不能抢占一个已经开始的节点恢复、回调或 GPU 操作；重工作必须继续按可控批次组织。

## 部署位置

- 编辑器只在明确项目根下读取 `settings/runtime-*.toml`，不把项目路径传给核心。
- 原生桌面包为程序旁 `settings/runtime.json`；macOS 可选 `.app` 为 `Contents/Resources/settings/runtime.json`。
- Android 为 APK `assets/settings/runtime.json`，宿主读取 `apk:/assets/settings/runtime.json`，使用 VFS 而非原生文件系统 exists 检查。这里的 assets 是平台容器，settings 与 catalog/gen/packs 并列。
- 打包聚合生成唯一 runtime.json，不携带任何 TOML、`editor-*.toml` 等工具设置或私有配置。JSON 只保留当前平台的完整结果；不携带 common/platform 分层。文件不加入 GUID catalog、不计作资源项，也不改变持久资源版本完整性规则。

## 执行和验证边界

文件实体读取复用 SDL AsyncIO，打开与路径检查是后台短任务，校验与解码交给加载任务域。操作固定目录及挂载快照，取消和换代只放弃结果，后台安全收尾。依赖图按后序在协调线程验证并发布；显式同步 load 仍可用于工具和启动。

GameplaySession 的正常 tick 只提交或消费一个部署候选。control、manifest、目录读取、文件摘要和状态写入使用后台域，结果携带请求代际。状态按最新文本合并并串行写出；worker 不访问活动场景、脚本 VM 或 App 全局状态。完整部署摘要目前使用限为一个后台任务的同步流式文件工具，仍占用该域一个 worker；显式启动和关闭收尾允许同步完成。

Tracy 复用既有构建开关，观察 `Task.execute` 的域、任务名、优先级及排队时间，以及 `File.prepare`、`File.complete`、`Resource.decode`、`Gameplay.deployment`。`File.InFlight` 和 `File.InFlightBytes` 用于检查 I/O 限流是否生效。性能报告须区分主线程提交耗时、后台 CPU 工作、排队等待和整个场景就绪耗时，不把工作移走等同于总耗时下降。

## 线程优先度与失败语义

`ThreadPriority` 为独立 OS 策略：normal 保持基线，low/high 在函数执行期间临时设置并精确恢复；权限不足仍执行已接收函数并报告失败，不因调度提示失败丢失 Jolt job。返回失败不代表函数未执行，不能自动重跑有副作用的任务。Apple 使用 QoS，Windows 使用原生线程优先级，Linux/Android 使用可恢复的调度策略和 nice。所有档位均不保证实时响应。`ENGINE_ENABLE_TRACY` 控制任务执行、排队和关闭等待的观测，默认关闭。
