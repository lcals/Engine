# 动画系统

[类图索引](../../class-diagrams/index.md) · [引擎与系统](engine-system.md) · [完整引用关系](../../class-diagrams/references.md)

本图定义 `AnimationSystem` 与其采样配置；其他节点为跨图引用或第三方类型。

**2026-11-07 已实现：** 离线归档消费和下面的缓存分工已实现；公共方法不新增，播放、重定向、事件、IK 与挂点契约保持。格式唯一来源为[模型资源](model-resources.md#离线采样产物)。

```mermaid
classDiagram
    direction TB
    class AnimationSystem {
        <<final>>
        +AnimationSystem(WorldSystem* world = nullptr, TaskSystem* tasks = nullptr, const AnimationConfig& config = {}, unique_ptr~AnimationRetargeter~ retargeter = nullptr)
        +~AnimationSystem()
        +clear() void
        +set_event_sink(AnimationEventSink sink) void
        #on_initialize(const EngineConfig& config) bool
        #on_tick(double delta_time) bool
        #on_shutdown() void
    }
    class System {
        <<abstract>>
    }
    class EngineConfig {
        <<struct>>
    }
    class AnimationConfig {
        <<struct>>
        +size_t min_parallel_count = 8
        +size_t batch_size = 16
    }
    class TaskSystem
    class AnimationRetargeter
    class AnimationEventSink
    class ozz["ozz-animation"] {
        <<library>>
    }
    class WorldSystem
    class Animator
    class MeshRenderer
    System <|-- AnimationSystem
    WorldSystem *-- AnimationSystem : 宿主装配后的子系统所有权
    AnimationSystem --> WorldSystem : 可空借用，存活期覆盖系统
    AnimationSystem --> TaskSystem : 可空借用，完成本帧任务后返回
    AnimationSystem *-- AnimationRetargeter : 可空注入，独占提供者
    AnimationSystem *-- AnimationEventSink : 可空主线程完成回调
    AnimationSystem *-- AnimationConfig : 保存配置值
    AnimationSystem ..> Animator : 读取播放请求，推进时间
    AnimationSystem ..> MeshRenderer : 提交骨骼 palette 或清除瞬态变形
    AnimationSystem ..> EngineConfig : on_initialize 参数
    AnimationSystem ..> ozz : 编译及链接依赖
    note for AnimationSystem "继承 System 生命周期；析构中调用 shutdown()"
```

- 由 `WorldSystem` 独占，通过父系统统一管理生命周期。
- 同一个普通 `Node` 挂 `Animator` 和 `MeshRenderer`，不另建动画节点继承层次。
- 动画、骨架与 Mesh 是独立 GUID 资源。动画引用源骨架，Mesh 引用目标骨架；目标每个关节必须在源骨架有完整名称和父层级对应，再显式映射到 ozz 的 DFS 顺序。无显式 profile 时保持此严格兼容播放；指定重定向 profile 时通过注入提供者进行源／目标参考姿态和体型转换，失败保持旧播放。缺整条轨道才使用绑定局部姿态，坏轨道失败。
- 私有场景缓存分开共享源采样数据与目标映射：不可变骨架按资源身份缓存，源动画归档按片段及源骨架身份缓存一次；目标关节映射 / 重定向绑定再按源、目标和 profile 身份缓存，不能因多个目标骨架重复解包同一源片段。身份包含资源代际并由独立获取保活。各实例独占时间、采样上下文、局部姿态和 palette，不保留 ECS 列借用。
- 不可变编译缓存由当前采样条目共享，无存活使用者的编译动作与骨架在更新时回收；没有进程永久动画缓存。常驻 Animator 跨场景保留播放时间、混合状态、采样上下文和 palette。
- 磁盘固定资源从已校验的采样归档建立运行对象；只有显式 structured 内存资源使用 ozz 纯内存 Builder。采样继续使用 `SamplingJob`、双层 `BlendingJob` 与 `LocalToModelJob`。输出两组 Mesh 局部空间、目标 `SkeletonData` 原始关节索引顺序、列主序 float 矩阵，不是源动作索引或 ozz DFS 顺序。渲染系统以 compute 蒙皮一次，主画面、阴影和 AO 共用输出；动画不再每帧复制整个顶点流。
- 主线程收集播放请求、准备资源缓存并推进时间，再按 `batch_size` 分批提交独占实例的 ozz 采样、混合和矩阵计算；worker 不访问活动 Scene、Node、Animator 或 MeshRenderer。全部任务完成后，主线程提交 palette，本帧时序不变，不增加一帧延迟。
- `tasks` 可空；非空时必须已初始化并覆盖动画系统生命周期。待采样数低于 `min_parallel_count`、只有一个批次，或任务域达到在途上限拒绝整个批次时，使用相同采样函数串行完成；已接收任务失败按失败返回，不重复执行。两项配置均须大于零；并发额度和任务优先级由借用的任务域配置。
- 此入口在本次 tick 内等待采样完成，不允许其他协调线程在其间修改场景；尚不提供跨 tick 的 begin/finish，也不承诺与后续主线程阶段重叠。磁盘固定动画首次使用只解包已验证归档并建立映射，不运行 SkeletonBuilder / AnimationBuilder。显式 structured 内存资源保留生成成本；归档解包、映射和实例分配仍须单独计量，本批不宣称首帧完全无工作。
- 法线使用 inverse-transpose 矩阵，切线使用普通线性矩阵；Mesh 的绑定姿态和资源身份保持不变。
- 删除组件或停用条件在系统更新时清理对应变形及采样条目，回收无使用者的编译缓存。Animator 在停用期间仍保存播放状态及打断过渡快照。`clear()` 支持重复调用，释放系统采样条目及编译缓存；普通场景替换按 `WorldSystem::scenes()` 的存活节点增量清理；宿主完整停止才调用 `clear()`，不等同于销毁仍存活组件的全部状态。

C++ 限定符：构造、析构、`clear()` 及 `on_shutdown()` 为 `noexcept`；析构和受保护钩子标注 `override`，返回状态的钩子标注 `[[nodiscard]]`。

新增公开播放、IK、挂点及事件契约分别见[动画 rigging](animation-rigging.md)、[重定向](animation-retargeting.md)与[事件](animation-events.md)。
