# 音频系统

[类图索引](../../class-diagrams/index.md) · [音频组件](audio-components.md) · [运行时音频](../audio.md)

```mermaid
classDiagram
    direction TB
    class AudioSystem {
        <<final>>
        +AudioSystem(WorldSystem#42; world)
        +~AudioSystem()
        +clear() void
        +set_paused(paused bool) bool
        +paused() bool
        +voice_count() size_t
        +last_error() string_view
        #on_initialize(const EngineConfig& config) bool
        #on_tick(double delta_time) bool
        #on_shutdown() void
    }
    class System {
        <<abstract>>
    }
    class WorldSystem
    class AudioSource
    class AudioListener
    class Transform
    class EngineConfig
    class SDL_AudioStream {
        <<external>>
    }
    class SDL_AudioDevice {
        <<external>>
    }
    System <|-- AudioSystem
    AudioSystem --> "0..1" WorldSystem : 借用至关闭
    AudioSystem *-- "0..*" SDL_AudioStream : 独占声源流
    AudioSystem *-- "0..1" SDL_AudioDevice : 按需打开并关闭
    AudioSystem ..> AudioSource : 执行请求并回写状态
    AudioSystem ..> AudioListener : 读取首个启用监听器
    AudioSystem ..> Transform : 读取世界位置和朝向
    AudioSystem ..> EngineConfig : 初始化参数
```

`AudioSystem` 属于 `engine`，SDL 类型仅出现在私有实现。构造参数允许空世界，此时初始化和空更新不打开音频。设备或流失败通过返回值和 `last_error()` 表达；不会自动选 dummy 输出。所有公开操作由宿主主线程调用，`clear()` 与析构为 `noexcept`。

暂停声源仍占一个 voice；停止、禁用、删除或切场景在音频更新时释放，宿主主动 `clear()` 同步释放。后台暂停保留游标与组件状态。节点引用每帧通过当前场景与节点 ID 查找，系统不持有悬空节点指针。自然播放完毕后保留已打开的设备至场景切换、`clear()` 或关闭，防止截断输出缓冲中的尾音。
