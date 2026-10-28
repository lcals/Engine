# 物理系统

[类图索引](index.md) · [引擎与系统](engine-system.md) · [完整引用关系](references.md)

状态：本轮最小实现。仅定义 `PhysicsSystem` 的方法；其他节点为跨图引用或第三方类型。

```mermaid
classDiagram
    direction TB
    class PhysicsSystem {
        <<final>>
        +PhysicsSystem()
        +~PhysicsSystem()
        #on_initialize(const EngineConfig& config) bool
        #on_shutdown() void
    }
    class System {
        <<abstract>>
    }
    class EngineConfig {
        <<struct>>
    }
    class Jolt["Jolt"] {
        <<library>>
    }
    System <|-- PhysicsSystem
    PhysicsSystem ..> EngineConfig : on_initialize 参数
    PhysicsSystem ..> Jolt : 编译及链接依赖
    note for PhysicsSystem "继承 System 生命周期；析构中调用 shutdown()"
```

- 由 `WorldSystem` 独占，通过父系统统一初始化、更新和关闭。
- 当前调用 Jolt 版本一致性入口以验证编译和链接；尚未创建物理世界、刚体或执行物理模拟。
- 公开业务接口当前只有构造与析构；继承 `System` 的公共生命周期方法，不提前增加业务方法。初始化失败和清理遵守[系统约定](engine-system.md)。
- 不重写 `on_tick()`，沿用基类默认成功的空更新；当前无逐帧业务任务。

C++ 限定符：构造、析构及 `on_shutdown()` 为 `noexcept`；析构和受保护钩子标注 `override`，返回状态的钩子标注 `[[nodiscard]]`。
