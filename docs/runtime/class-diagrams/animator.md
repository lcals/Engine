# 动画组件

[类图索引](../../class-diagrams/index.md) · [动画系统](animation-system.md)

```mermaid
classDiagram
    class Animator {
        <<value handle>>
        +Animator()
        +Animator(Component component)
        +register_type(ComponentRegistry& registry) bool$
        <<final>>
        +valid_settings(const AnimatorDataT& settings)$ bool
        +settings() AnimatorDataT
        +get_settings(AnimatorDataT& output) bool
        +set_settings(const AnimatorDataT& settings) bool
        +restore_data(const ComponentDataT& data) bool
        +set_animation(string_view path) bool
        +play_animation(const AnimationPlayRequest& request) bool
        +get_playback_info(AnimatorPlaybackInfo& output) bool
        +get_bone_pose(string_view path, AnimationPoseTransform& output) bool
        +get_socket_pose(string_view name, AnimationPoseTransform& output) bool
        +set_aim_ik_target(string_view name, const AnimationIkTarget& target) bool
        +set_two_bone_ik_target(string_view name, const AnimationIkTarget& target) bool
        +get_ik_target(string_view name, AnimationIkTarget& output) bool
        +set_event_markers(span~const AnimationEventMarkerDataT~ markers) bool
        +clear_event_markers() void
        +cross_fade(string_view path, double seconds) bool
        +animation_path() string_view
        +play() bool
        +pause() bool
        +stop() bool
        +seek(double seconds) bool
        +playback() AnimationPlayback
        +time() double
    }
    class AnimationPlayRequest
    class AnimatorPlaybackInfo
    class AnimationPoseTransform
    class AnimationIkTarget
    class AnimationEventMarkerDataT
    Animator ..> AnimationPlayRequest : 原子播放输入
    Animator ..> AnimatorPlaybackInfo : 播放状态输出
    Animator ..> AnimationPoseTransform : 已提交姿态输出
    Animator ..> AnimationIkTarget : 实例目标输入和输出
    Animator ..> AnimationEventMarkerDataT : 实例事件覆层输入
    class Component
    class Node
    class AnimationSystem
    class MeshRenderer
    class Resource
    class AnimatorDataT {
        <<generated>>
    }
    class ComponentDataT {
        <<generated>>
    }
    class AnimationPlayback {
        <<enumeration>>
        stopped
        playing
        paused
    }
    Component <|-- Animator
    Node ..> Animator : 按类型返回值句柄
    AnimatorRuntime --> Resource : 共享动画与目标骨架
    Animator ..> MeshRenderer : 停止时恢复绑定姿态
    Animator ..> AnimatorDataT : 参数和返回值
    Animator ..> ComponentDataT : 恢复参数
    Animator ..> AnimationPlayback : 返回播放状态
    AnimationSystem ..> Animator : 推进与采样
    class ComponentRegistry
    class ecs_world_t
    class AnimatorRuntime {
        <<ECS runtime column>>
    }
    ecs_world_t *-- AnimatorRuntime : 实例运行数据
    Animator ..> AnimatorRuntime : 校验身份后短期借用
    Animator ..> ComponentRegistry : 静态登记类型操作

```

- 使用 `Node::add_component<Animator>()` 创建；公开接口无 ozz 类型。
- 持久设置来自 `AnimatorDataT`：独立动画 GUID、速度、循环、自动播放和默认关闭的 IK。播放时间、状态和采样缓存不进入场景数据。
- `set_settings` / `set_animation` 资源切换或数值校验失败保持原设置与资源；空路径清空动画。这两项只检查设置与 Animation 资源，不同步检查 Mesh/骨架绑定，兼容性错误由动画系统准备阶段返回。
- `cross_fade(path, seconds)` 先校验资源、目标骨架层级与蒙皮绑定，再切换目标动作；时长有限且非负，零秒直接切换。目标从零秒播放，来源保留切换时的速度和循环设置。新动作的速度和循环仍通过原有设置调整；相同 GUID 和代际且当前未停止时直接成功返回，不重新开始、不解除暂停，也不改变已有过渡。
- 过渡使用两个局部姿态经 ozz BlendingJob 混合。过渡中再次切换时，以当前混合姿态快照作为新来源，不增长混合树。暂停冻结播放和过渡；停止、直接换动作或定位取消过渡。过渡秒数不受动作速度影响。
- 显式 `set_animation` 即使路径相同也取消过渡，同路径保持原播放时间。打断来源快照由组件暂存，禁用期间释放采样与 GPU 输出，重新启用可恢复同一混合姿态；快照不进入场景持久数据。
- 玩法脚本或插件根据移动、落地和受击选择动作；核心不定义待机、行走、跳跃等业务状态。旧模型与新模型使用同一组件、采样和 GPU 蒙皮路径。
- `play()` 从停止状态重新开始，从暂停状态继续。`pause()` 保留最后姿态；`stop()` 将时间归零并恢复 Mesh 绑定姿态。
- `seek()` 接受当前动画 `[0, duration]` 内的秒数；下一次系统更新提交该姿态。停止时定位会进入暂停以保留该姿态，播放中定位保持播放。速度范围为 `0..16`，零速度保留时间；单次播放到结尾进入暂停并保留最后姿态。
- 可能失败的方法返回 `bool` 并标注 `[[nodiscard]]`；读取方法为 `const`，无外部分配或资源加载的控制方法为 `noexcept`。

本页组件均为可复制值句柄，生成设置唯一存于 `ComponentStorage<Data>` 的 ECS 列；`settings()` 返回独立值，`get_settings(output)` 明确返回失败。Runtime 为 ECS 中的领域运行列，资源引用、请求和缓存归该列或所属 System；句柄不拥有这些数据。Runtime 定义与可变访问仅供所属模块内部实现使用，公开组件不提供 `runtime()`；原生系统的 ECS 列借用仅在当前结构稳定阶段有效，不能跨结构修改留存。类型注册、失效身份与查询规则见 [ECS 场景](ecs-scene.md)。

本页维护 Animator 当前完整原生公开方法；请求、值快照和执行时序分别见[动画 rigging](animation-rigging.md)与[动画事件](animation-events.md)，专题图只引用本页的方法契约。
