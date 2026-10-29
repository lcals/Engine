# 导航系统

[类图索引](../../class-diagrams/index.md) · [引擎与系统](engine-system.md) · [完整引用关系](../../class-diagrams/references.md)

状态：本轮最小实现。仅定义 `NavigationSystem` 的方法；其他节点为跨图引用或第三方类型。

```mermaid
classDiagram
    direction TB
    class NavigationSystem {
        <<final>>
        +NavigationSystem()
        +~NavigationSystem()
        #on_initialize(const EngineConfig& config) bool
        #on_shutdown() void
    }
    class System {
        <<abstract>>
    }
    class EngineConfig {
        <<struct>>
    }
    class Recast["Recast"] {
        <<library>>
    }
    class Detour["Detour"] {
        <<library>>
    }
    class rcHeightfield {
        <<external>>
    }
    class dtNavMesh {
        <<external>>
    }
    System <|-- NavigationSystem
    NavigationSystem ..> EngineConfig : on_initialize 参数
    NavigationSystem ..> Recast : 编译及链接依赖
    NavigationSystem ..> Detour : 编译及链接依赖
    NavigationSystem "1" *-- "0..1" rcHeightfield : 拥有高度场资源
    NavigationSystem "1" *-- "0..1" dtNavMesh : 拥有导航网格资源
    note for NavigationSystem "继承 System 生命周期；析构中调用 shutdown()"
```

- 由 `WorldSystem` 独占，通过父系统统一管理生命周期。
- 当前使用 Recast 和 Detour 的分配接口创建空高度场及空导航网格，并通过配对释放接口销毁；尚未烘焙地图或执行寻路。
- 公开业务接口当前只有构造与析构；继承 `System` 的公共生命周期方法，不提前增加业务方法。初始化失败和清理遵守[系统约定](engine-system.md)。
- 不重写 `on_tick()`，沿用基类默认成功的空更新；当前无逐帧业务任务。

C++ 限定符：构造、析构及 `on_shutdown()` 为 `noexcept`；析构和受保护钩子标注 `override`，返回状态的钩子标注 `[[nodiscard]]`。
