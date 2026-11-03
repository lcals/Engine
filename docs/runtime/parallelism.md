# 运行并行与项目预算

[运行设置](class-diagrams/runtime-settings.md) · [运行宿主类图](class-diagrams/runtime-host.md) · [线程与任务](class-diagrams/task-system.md) · [资源加载](class-diagrams/resource-loader.md) · [玩法会话](class-diagrams/gameplay-session.md)

桌面和移动端共用同一套并行实现。平台宿主明确选择预算档位，项目只调额度，脚本不管理线程、线程池或跨线程场景状态。主线程继续负责平台事件、当前 UI/VM、场景生命周期和结果发布。

## 配置来源

项目源配置仅由编辑器和打包工具读取：`settings/runtime.toml` 汇总核心窗口和帧率设置，`settings/parallel.toml` 保存任务及领域预算。两份文件均可省略；存在时必须有效。它们最终合成一个 `RuntimeSettings`，由调用方把 `engine` 和 `parallel` 类型值交给 RuntimeHost。运行时不依赖 TOML、不读取项目目录。

`runtime.toml` 示例：

```toml
version = 1
[defaults.engine]
width = 1280
height = 720
target_frame_rate = -1
# window_title = "My game"

[profiles.mobile.engine]
target_frame_rate = 60
```

未填写的标题保留宿主默认；编辑器默认仍是 Engine Editor。宽高必须为正数且能安全传入 SDL 的 int 参数，帧率为 -1 或正 int32。其他编辑器设置、导入并发和打包入口继续留在各自已有文件，不进入核心配置。

`parallel.toml` 示例：

```toml
version = 1

[defaults.threads]
worker_limit = 0
worker_priority = "normal"

[defaults.resources]
max_in_flight = 4
max_in_flight_bytes = 67108864

[profiles.desktop.loading]
max_concurrency = 4

[profiles.mobile.threads]
worker_limit = 3

[profiles.mobile.loading]
max_concurrency = 1

[overrides]
```

按调用方默认值 → `defaults` → 明确选择的 `profiles.desktop/mobile` → 项目 `overrides` 依次覆盖；显式 CLI `--width/--height`（编辑器）与 `--workers` 最后应用。`--runtime-profile desktop|mobile` 由宿主选择整个运行档位。未填写字段保留前层值，移动线程数是可调初值，实际设备调整以持续性能采样为依据。公开范例 showcase 已提供两份源配置。

编辑器 `load_project_runtime_settings()` 读取 TOML，按相同结构汇总并调用 `decode_runtime_settings()`；打包器输出同一模型的 JSON：

```json
{
  "version": 1,
  "defaults": {"engine": {"width": 1280}, "parallel": {"threads": {"worker_limit": 0}}},
  "profiles": {"mobile": {"parallel": {"threads": {"worker_limit": 3}}}},
  "overrides": {}
}
```

运行宿主只使用 `load_runtime_settings()` 读取这份 JSON，或直接传入已有类型值。公开普通函数与所有权见[运行设置类图](class-diagrams/runtime-settings.md)。JSON 接收上限 64 KiB；未知字段、错误类型、非法范围和其他档位中的无效配置都会被拒绝，失败不改变调用方输出。打包只做源结构汇总，完整字段语义由共用 C++ 解析器验证；实际导出 JSON 可作为 `engine_runtime_settings_test` 的路径参数检查。继续新增运行参数时，扩展相应类型及配置字段，不新增独立配置管理系统。

待补边界：独立打包工具尚未在发布前执行上述完整字段语义验证；绕过编辑器直接打包非法字段时，会在宿主启动读取配置时失败。本轮两项目源及实际导出的 JSON 已通过原生校验。后续应复用原生解析器增加发布前校验，同时保留 Android 从已构建资源目录打包的流程，避免在 Python 复制整套参数规则。

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
| `completions_per_frame` / `completion_budget_seconds` | 主线程完成委托派发数量与总时间预算；数量 1–65536，秒数 (0, 1] |

配置在启动时读取。本轮不自动重读文件，不承诺运行中任意缩减 worker 或域额度。时间预算在工作项之间检查，不能抢占一个已经开始的节点恢复、回调或 GPU 操作；重工作必须继续按可控批次组织。

## 部署位置

- 编辑器只在明确项目根下读取 `settings/runtime.toml` 与 `settings/parallel.toml`，不把项目路径传给核心。
- 原生桌面包为程序旁 `settings/runtime.json`；macOS 可选 `.app` 为 `Contents/Resources/settings/runtime.json`。
- Android 为 APK `assets/settings/runtime.json`，宿主读取 `apk:/assets/settings/runtime.json`，使用 VFS 而非原生文件系统 exists 检查。这里的 assets 是平台容器，settings 与 catalog/gen/packs 并列。
- 打包聚合生成唯一 runtime.json，不携带任何 TOML、editor/import 等工具设置或私有配置。无显式覆盖的项目部署 `{"version":1}`，由宿主提供默认值。文件不加入 GUID catalog、不计作资源项，也不改变持久资源版本完整性规则。

## 执行和验证边界

文件实体读取复用 SDL AsyncIO，打开与路径检查是后台短任务，校验与解码交给加载任务域。操作固定目录及挂载快照，取消和换代只放弃结果，后台安全收尾。依赖图按后序在协调线程验证并发布；显式同步 load 仍可用于工具和启动。

GameplaySession 的正常 tick 只提交或消费一个部署候选。control、manifest、目录读取、文件摘要和状态写入使用后台域，结果携带请求代际。状态按最新文本合并并串行写出；worker 不访问活动场景、脚本 VM 或 App 全局状态。完整部署摘要目前使用限为一个后台任务的同步流式文件工具，仍占用该域一个 worker；显式启动和关闭收尾允许同步完成。

Tracy 复用既有构建开关，观察 `Task.execute` 的域、任务名、优先级及排队时间，以及 `File.prepare`、`File.complete`、`Resource.decode`、`Gameplay.deployment`。`File.InFlight` 和 `File.InFlightBytes` 用于检查 I/O 限流是否生效。性能报告须区分主线程提交耗时、后台 CPU 工作、排队等待和整个场景就绪耗时，不把工作移走等同于总耗时下降。

## 线程优先度与失败语义

`ThreadPriority` 为独立 OS 策略：normal 保持基线，low/high 在函数执行期间临时设置并精确恢复；权限不足仍执行已接收函数并报告失败，不因调度提示失败丢失 Jolt job。返回失败不代表函数未执行，不能自动重跑有副作用的任务。Apple 使用 QoS，Windows 使用原生线程优先级，Linux/Android 使用可恢复的调度策略和 nice。所有档位均不保证实时响应。`ENGINE_ENABLE_TRACY` 控制任务执行、排队和关闭等待的观测，默认关闭。
