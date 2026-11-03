# 运行时线程与任务系统

[类图索引](../../class-diagrams/index.md) · [资源管线](../../editor/class-diagrams/asset-pipeline-system.md)

oneTBB 管理进程共享 worker，编辑器与运行时各有任务域。公开头仅依赖标准库和 System；旧 editor/task 实现下沉 runtime/task，不保留兼容别名。

```mermaid
classDiagram
    class System
    class EngineConfig
    class ThreadConfig {
        +size_t worker_limit
        +ThreadPriority worker_priority
    }
    class ThreadPriority {
        <<enumeration>>
        low
        normal
        high
    }
    class ThreadSystem {
        +ThreadSystem(const ThreadConfig& config)
        +~ThreadSystem()
        +worker_count() size_t
        +worker_priority() ThreadPriority
        +last_error() const string&
    }
    class TaskConfig {
        +string name
        +size_t max_concurrency
        +TaskPriority priority
        +size_t max_pending
    }
    class TaskPriority {
        <<enumeration>>
        low
        normal
        high
    }
    class TaskDescription {
        +string name
        +function~bool()~ execute
        +vector~size_t~ dependencies
        +TaskPriority priority
    }
    class TaskSystem {
        +TaskSystem(ThreadSystem& threads, const TaskConfig& config)
        +~TaskSystem()
        +configure_concurrency(size_t count) bool
        +submit(string name, function~bool()~ task, TaskPriority priority) future~bool~
        +submit_batch(vector~TaskDescription~ tasks, TaskPriority priority) future~bool~
        +submit(string name, function~bool()~ task, function~void(bool)~ completed, TaskPriority priority) bool
        +enqueue(string name, function~void()~ task, TaskPriority priority) bool
        +dispatch_completions(size_t max_count, double max_seconds) size_t
        +run(const vector~TaskDescription~& tasks) bool
        +wait() bool
        +max_concurrency() size_t
        +last_error() const string&
    }
    class GlobalControl {
        <<oneTBB private>>
    }
    class TaskArena {
        <<oneTBB private>>
    }
    class FlowGraph {
        <<oneTBB private>>
    }
    class RuntimeHost
    class AssetPipelineSystem
    class HostEntry {
        <<executable assembly>>
    }
    System <|-- ThreadSystem
    System <|-- TaskSystem
    ThreadSystem *-- ThreadConfig
    ThreadConfig ..> ThreadPriority
    ThreadSystem ..> EngineConfig : initialization
    ThreadSystem *-- GlobalControl : one process budget
    TaskSystem --> ThreadSystem : borrows until shutdown
    TaskSystem *-- TaskConfig
    TaskConfig ..> TaskPriority
    TaskSystem *-- TaskArena : independent domain
    TaskSystem *-- FlowGraph : permanent priority entry nodes
    TaskSystem ..> TaskDescription : owns submitted copies
    TaskDescription ..> TaskPriority
    TaskSystem ..> EngineConfig : initialization
    HostEntry *-- ThreadSystem : editor process or offline tool
    HostEntry *-- TaskSystem : editor domain
    HostEntry ..> AssetPipelineSystem : injects editor domain
    AssetPipelineSystem --> TaskSystem : initialized borrowed service
    RuntimeHost *-- ThreadSystem : standalone only
    RuntimeHost --> ThreadSystem : editor supplies shared owner
    RuntimeHost *-- TaskSystem : runtime domain
```

## 所有权与生命周期

ThreadSystem 是唯一进程并行预算拥有者；第二个实例不能同时初始化。ThreadConfig.worker_limit 默认 0（按可用 CPU 留一主线程额度，至少 1、最多 64），显式值 1–64。global_control 使用 worker_limit + 1；这是引擎预算，不是独占或预创建的线程数，其他库更低的全局限制仍会生效。ThreadPriority 默认 normal。

TaskSystem 借用已初始化的 ThreadSystem，各自拥有 arena、入口图和关闭等待。TaskConfig 默认 name="Tasks"、max_concurrency=4、priority=normal；额度范围 1–64，实际执行同时受进程预算限制。两域不预留独占 worker，关闭一域只等待该域工作。所有域先关闭，ThreadSystem 最后关闭。

生命周期、提交、配置及 last_error 查询由所属协调线程调用。回调不得调用 System 生命周期、同步等待同池 future；依赖用批次表达。run/wait/configure_concurrency 在回调内拒绝。提交拥有函数和数据的副本；闭包借用由调用方保证活到 future 完成。关闭停止接收、等待已接收工作、释放图和 arena；不强制打断正在写文件的函数。支持重复关闭、部分初始化与重新初始化。

编辑器入口拥有 ThreadSystem 和低优先 TaskSystem，向 AssetPipelineSystem 注入已初始化 TaskSystem&；管线不再自建池。import_threads 默认 4，当前在空闲时配置编辑器域，有在途任务时不能改变额度。整批导入仍由协调线程同步组织，仅转换阶段在 worker 执行。

RuntimeHost 通过可空参数借用已有 ThreadSystem；独立运行时自行创建。宿主独占运行、后台、加载、物理、动画和导航任务域，tasks() 返回运行域的可空借用；额度与优先级由 RuntimeParallelConfig 配置。关闭先停止玩法和资源异步服务，再销毁各领域系统并等待其已接受工作，最后关闭任务域与自有线程预算，不能先关域再等 Jolt barrier。

## 提交、依赖与优先级

- `TaskConfig.max_pending` 默认 1024，限制尚未完成的请求与尚未派发的完成委托总数；满额拒绝提交，不阻塞生产者。一个批次算一个请求，调用方仍须限制批次大小。
- 委托版 `submit` 在 worker 执行计算，在初始化线程显式 `dispatch_completions` 执行完成委托；每次同时受数量和秒数预算限制，任一为 0 不派发。委托本身不可抢占，调用方应保持短小。派发先释放该请求额度，允许委托提交下一阶段；同一轮不递归派发新完成委托。
- `enqueue` 是 Jolt 等已有 job 系统的最小桥接，允许 worker 动态投递，无 future、无完成委托。接收即保证函数执行；失败返回 false，不跨线程写入 last_error。关闭开始后拒绝新工作，使用者必须先完成自身 job/barrier。
- OS 优先级设置失败记录 Tracy 消息，但仍执行已接收函数；有返回值任务报告优先级或工作失败。该失败不能让 Jolt 丢失 job 而永久等待。
- 关闭等待工作执行结束，丢弃未派发的完成委托；不在析构期间调用业务。请求取消、代际校验和数据生命周期继续由所属模块管理。

- submit(name, task, priority=normal) / submit_batch(tasks, priority=normal) 返回 std::future<bool>。未初始化、空函数、非法枚举、无效/重复依赖或环，返回无效 future 并设置 last_error；接收后的结果表示整项成功/失败。空批次返回已完成 true。
- last_error 仅报告协调线程上的提交、配置和同步 run 错误；worker 不并发改写它。异步回调失败或 OS 优先级设置/恢复失败，通过 future 返回 false，任务身份可在 Tracy 追踪。
- 一个长期 flow graph 的三档 function_node 负责单任务与批次入口，优先度使用原生 node_priority_t。每个入口的活跃请求上限为域并发额度，由原生队列暂存其余请求；三档合计最多 3 倍额度的活跃请求体，避免协作等待无限嵌套，实际执行线程数仍受 arena 限制。TaskConfig.priority 映射 arena 优先度，控制域间调度倾向。
- 批次在 worker 内建立临时 flow graph；continue_node 使用 TaskDescription.priority，边由 oneTBB 调度。失败的下游跳过，独立分支仍完成。graph.wait_for_all 使用 oneTBB 协作执行，不能替换成 worker 阻塞等 std::future；图彻底结束才销毁节点。
- 批次优先度控制入口，各节点优先度控制该图内就绪工作；不承诺跨所有图严格排序。优先度均非抢占，不能打断长函数或越过依赖。
- run 为同步导入提供便利；wait/关闭先等待本域完成计数，再收尾原生图，调用线程不执行用户函数。异步消费者通常 wait_for(0) 后按主线程预算应用结果。

## OS 优先度与 Tracy

ThreadPriority 独立于 TaskPriority。worker_priority=normal 保持 worker 基线；low/high 仅执行引擎函数期间临时设置并恢复，不能永久给共享 worker 贴域标签。Apple 使用 QoS，Windows 使用线程优先度，Linux/Android 使用可恢复的调度策略/nice。设置失败仍执行已接收函数并记录观测消息；有结果的任务在设置或恢复失败时返回 false，因此 false 不代表函数没有副作用，调用者不能据此自动重跑。无权限提升不能报告成功。UI/输入仍属于主线程关键路径。

复用 ENGINE_ENABLE_TRACY；记录任务名、域、优先度、入队到执行等待、批次与关闭等待。排队纳秒与域、任务名和优先级合成一条单行注释，避免官方 TSV 导出的多行问题；批次节点的排队时间包含依赖等待，不代表依赖就绪后的纯排队延迟。不伪造跨线程 zone。关闭开关不链接客户端。

## 边界

文件、物理、导航、动画、纹理准备和地形/破坏已按各自契约接入，SDL 音频与 bgfx 复用其官方线程。活动 Scene/Node/Component、VM、GPU 提交保持原线程契约；并行计算、跨帧异步和同步回退的区别见[运行并行与项目预算](../parallelism.md)。资源代际校验、主线程应用预算归具体模块，不放进通用任务系统。

依赖使用现有固定 oneTBB 版本和补丁；平台声明与实际测试分开记录于[测试与范例](../../testing.md)。Mac 动态库通过不代表 Android/iOS 或静态链接已验证。
