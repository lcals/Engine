# 动画组件

[类图索引](../../class-diagrams/index.md) · [动画系统](animation-system.md)

```mermaid
classDiagram
    class Animator {
        <<final>>
        +valid_settings(const AnimatorDataT& settings)$ bool
        +settings() const AnimatorDataT&
        +set_settings(const AnimatorDataT& settings) bool
        +restore_data(const ComponentDataT& data) bool
        +set_animation(string_view path) bool
        +cross_fade(string_view path, double seconds) bool
        +animation_path() string_view
        +play() bool
        +pause() bool
        +stop() bool
        +seek(double seconds) bool
        +playback() AnimationPlayback
        +time() double
    }
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
    Node *-- Animator : 独占组件
    Animator --> Resource : 共享动画与目标骨架
    Animator ..> MeshRenderer : 停止时恢复绑定姿态
    Animator ..> AnimatorDataT : 参数和返回值
    Animator ..> ComponentDataT : 恢复参数
    Animator ..> AnimationPlayback : 返回播放状态
    AnimationSystem ..> Animator : 推进与采样
```

- 使用 `Node::add_component<Animator>()` 创建；公开接口无 ozz 类型。
- 持久设置来自 `AnimatorDataT`：独立动画 GUID、速度、循环、自动播放。播放时间、状态和采样缓存不进入场景数据。
- `set_settings` / `set_animation` 资源切换或数值校验失败保持原设置与资源；空路径清空动画。这两项只检查设置与 Animation 资源，不同步检查 Mesh/骨架绑定，兼容性错误由动画系统准备阶段返回。
- `cross_fade(path, seconds)` 先校验资源、目标骨架层级与蒙皮绑定，再切换目标动作；时长有限且非负，零秒直接切换。目标从零秒播放，来源保留切换时的速度和循环设置。新动作的速度和循环仍通过原有设置调整；相同 GUID 和代际且当前未停止时直接成功返回，不重新开始、不解除暂停，也不改变已有过渡。
- 过渡使用两个局部姿态经 ozz BlendingJob 混合。过渡中再次切换时，以当前混合姿态快照作为新来源，不增长混合树。暂停冻结播放和过渡；停止、直接换动作或定位取消过渡。过渡秒数不受动作速度影响。
- 显式 `set_animation` 即使路径相同也取消过渡，同路径保持原播放时间。打断来源快照由组件暂存，禁用期间释放采样与 GPU 输出，重新启用可恢复同一混合姿态；快照不进入场景持久数据。
- 玩法脚本或插件根据移动、落地和受击选择动作；核心不定义待机、行走、跳跃等业务状态。旧模型与新模型使用同一组件、采样和 GPU 蒙皮路径。
- `play()` 从停止状态重新开始，从暂停状态继续。`pause()` 保留最后姿态；`stop()` 将时间归零并恢复 Mesh 绑定姿态。
- `seek()` 接受当前动画 `[0, duration]` 内的秒数；下一次系统更新提交该姿态。停止时定位会进入暂停以保留该姿态，播放中定位保持播放。速度范围为 `0..16`，零速度保留时间；单次播放到结尾进入暂停并保留最后姿态。
- 可能失败的方法返回 `bool` 并标注 `[[nodiscard]]`；读取方法为 `const`，无外部分配或资源加载的控制方法为 `noexcept`。
