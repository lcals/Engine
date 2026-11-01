# 动画系统

[类图索引](../../class-diagrams/index.md) · [引擎与系统](engine-system.md) · [完整引用关系](../../class-diagrams/references.md)

本图仅定义 `AnimationSystem` 的方法；其他节点为跨图引用或第三方类型。

```mermaid
classDiagram
    direction TB
    class AnimationSystem {
        <<final>>
        +AnimationSystem(WorldSystem* world = nullptr)
        +~AnimationSystem()
        +clear() void
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
    class ozz["ozz-animation"] {
        <<library>>
    }
    class WorldSystem
    class Animator
    class MeshRenderer
    System <|-- AnimationSystem
    WorldSystem *-- AnimationSystem : 宿主装配后的子系统所有权
    AnimationSystem --> WorldSystem : 可空借用，存活期覆盖系统
    AnimationSystem ..> Animator : 读取播放请求，推进时间
    AnimationSystem ..> MeshRenderer : 提交骨骼 palette 或清除瞬态变形
    AnimationSystem ..> EngineConfig : on_initialize 参数
    AnimationSystem ..> ozz : 编译及链接依赖
    note for AnimationSystem "继承 System 生命周期；析构中调用 shutdown()"
```

- 由 `WorldSystem` 独占，通过父系统统一管理生命周期。
- 同一个普通 `Node` 挂 `Animator` 和 `MeshRenderer`，不另建动画节点继承层次。
- 动画、骨架与 Mesh 是独立 GUID 资源。动画引用源骨架，Mesh 引用目标骨架；目标每个关节必须在源骨架有完整名称和父层级对应，再显式映射到 ozz 的 DFS 顺序。该匹配直接使用源局部 TRS，不做参考姿态或体型补偿；缺整条轨道才使用目标默认局部姿态，缺关节或坏轨道失败。
- 私有场景缓存共享不可变 ozz 骨架和动画；身份包括 GUID 与资源代际，动画还包括源和目标骨架身份。各实例独占时间、采样上下文、局部姿态和 palette，只缓存场景/节点 ID，不保留节点借用。
- 场景作用域强缓存保留本场景已使用的编译动作，避免待机/行走往返时重复 Builder。`clear()`、换场景和结束玩法释放全部编译缓存；没有进程永久动画缓存。
- 从已校验的 FlatBuffers 数据通过 ozz 纯内存 Builder 建立运行对象；使用 `SamplingJob`、双层 `BlendingJob` 与 `LocalToModelJob`。输出两组 Mesh 局部空间、目标 `SkeletonData` 原始关节索引顺序、列主序 float 矩阵，不是源动作索引或 ozz DFS 顺序。渲染系统以 compute 蒙皮一次，主画面、阴影和 AO 共用输出；动画不再每帧复制整个顶点流。
- 法线使用 inverse-transpose 矩阵，切线使用普通线性矩阵；Mesh 的绑定姿态和资源身份保持不变。
- 删除组件或停用条件在系统更新时清理对应变形及采样条目，不清除本场景编译缓存。Animator 在停用期间仍保存播放状态及打断过渡快照。`clear()` 支持重复调用，释放系统采样条目及编译缓存；场景替换或宿主结束玩法时同样清理，不等同于销毁仍存活组件的全部状态。

C++ 限定符：构造、析构、`clear()` 及 `on_shutdown()` 为 `noexcept`；析构和受保护钩子标注 `override`，返回状态的钩子标注 `[[nodiscard]]`。
