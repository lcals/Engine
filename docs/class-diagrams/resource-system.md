# 资源系统

[类图索引](index.md) · [引擎与系统](engine-system.md) · [完整引用关系](references.md)

状态：本轮最小实现。仅定义 `ResourceSystem` 的方法；其他节点为跨图引用或第三方类型。

```mermaid
classDiagram
    direction TB
    class ResourceSystem {
        <<final>>
        +ResourceSystem()
        +~ResourceSystem()
        #on_initialize(const EngineConfig& config) bool
        #on_shutdown() void
    }
    class System {
        <<abstract>>
    }
    class EngineConfig {
        <<struct>>
    }
    class FlatBuffers["FlatBuffers"] {
        <<library>>
    }
    class BasisUniversal["Basis Universal"] {
        <<library>>
    }
    System <|-- ResourceSystem
    ResourceSystem ..> EngineConfig : on_initialize 参数
    ResourceSystem ..> FlatBuffers : 编译及链接依赖
    ResourceSystem ..> BasisUniversal : 编译及链接依赖
    note for ResourceSystem "继承 System 生命周期；析构中调用 shutdown()"
```

- 由 `Engine` 拥有的根系统；先于世界和渲染初始化。
- 当前初始化 Basis Universal 转码表并调用 FlatBuffers 解析入口，验证真实编译和链接；资源格式、加载和缓存接口后续按需要增加。
- Basis Universal 的共享查找表由库管理，系统没有可单独销毁的转码表实例。
- 公开业务接口当前只有构造与析构；继承 `System` 的公共生命周期方法，不提前增加业务方法。初始化失败和清理遵守[系统约定](engine-system.md)。
- 不重写 `on_tick()`，沿用基类默认成功的空更新；当前无逐帧业务任务。

C++ 限定符：构造、析构及 `on_shutdown()` 为 `noexcept`；析构和受保护钩子标注 `override`，返回状态的钩子标注 `[[nodiscard]]`。
