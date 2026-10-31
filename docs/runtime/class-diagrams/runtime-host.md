# 运行宿主

[类图索引](../../class-diagrams/index.md) · [完整引用关系](../../class-diagrams/references.md)

```mermaid
classDiagram
    direction TB
    class RuntimeHost {
        +RuntimeHost()
        +~RuntimeHost()
        +initialize(const EngineConfig& config, ResourceSystem#42; resources, ScriptRuntimeFactory script_factory, RuntimeSetup setup) bool
        +tick(double delta_time) FrameStatus
        +gameplay() GameplaySession#42;
        +world() WorldSystem#42;
        +window() WindowSystem#42;
        +renderer() RenderSystem#42;
        +request_exit() void
        +shutdown() void
    }
    class Engine
    class WindowSystem
    class GameplaySession
    class RenderSystem
    class PerformanceSystem
    class WorldSystem
    class PhysicsSystem
    class AudioSystem
    class ResourceSystem
    class ScriptRuntime
    class ScriptRuntimeFactory
    class RuntimeSetup
    class RuntimeStage {
        <<enumeration>>
        before_physics
        after_physics
    }
    class AnimationSystem
    class EngineConfig
    class FrameStatus
    RuntimeHost --> Engine : 借用进程框架并装配根系统
    RuntimeHost *-- WindowSystem : 独占窗口
    RuntimeHost *-- GameplaySession : 独占运行会话
    RuntimeHost --> RenderSystem : 借用以提交帧
    RuntimeHost ..> PerformanceSystem : 创建并移交引擎独占
    RuntimeHost --> WorldSystem : 借用场景状态
    RuntimeHost --> PhysicsSystem : 装配后借用
    RuntimeHost --> AudioSystem : 借用以暂停及恢复音频
    RuntimeHost --> FontSystem : 装配后借用字体运行时
    RuntimeHost --> UiSystem : 装配后借用并传递窗口输入
    RuntimeHost --> ResourceSystem : 可选借用宿主已初始化底层
    RuntimeHost ..> ScriptRuntimeFactory : 可选宿主创建函数
    RuntimeHost ..> RuntimeSetup : 初始化前调用宿主装配函数
    RuntimeSetup ..> RuntimeStage : 选择子系统登记位置
    RuntimeHost --> AnimationSystem : 装配后借用以清理会话
    RuntimeHost ..> ScriptRuntime : 创建后移交世界独占
    RuntimeHost ..> EngineConfig : 初始化参数
    RuntimeHost ..> FrameStatus : 帧状态返回值
```

- RuntimeHost 位于运行时 app 模块，是桌面、编辑器和 Android 共用的运行组合入口。类型为 final，禁止复制和移动；构造、析构、查询、退出请求和关闭为 noexcept；initialize、tick 和 gameplay 的返回值标注 nodiscard。
- 初始化先独立创建 WindowSystem；resources 为空时向 Engine 注册并拥有 ResourceSystem，非空时借用宿主已经初始化的资源底层，再装配 PerformanceSystem、WorldSystem、RenderSystem。性能系统是独立根系统，当前仅接入可选 Tracy 客户端；世界独占物理、音频、导航、动画、字体、界面和可选脚本子系统。引擎已被其他宿主使用时拒绝初始化，不清理他人的资源。
- `resources`、`script_factory` 和 `setup` 默认空。`ScriptRuntimeFactory` 是 `std::unique_ptr<ScriptRuntime> (*)(WorldSystem&, RenderSystem&, UiSystem&)` 函数指针别名，由可执行宿主选择插件并提供。先构造世界和渲染器，再调用工厂创建尚未初始化的抽象脚本子系统，交给世界统一初始化、更新和关闭；初始化仍按世界、渲染器的次序。非空工厂返回空或无法附属时，整个初始化失败并清理已取得的资源。渲染器借用仅用于玩法回调；关闭时先由 GameplaySession 停止回调，再释放渲染器和世界。
- RuntimeHost 和 GameplaySession 只依赖 [ScriptRuntime](script-runtime.md) 契约，不包含或链接具体脚本插件，不依据虚拟机或插件编译宏改变核心实现。无工厂时仍能初始化窗口、场景和渲染；启动脚本玩法明确返回不可用。
- 每帧先处理窗口事件，退出返回 exit_requested；窗口不可绘制时暂停音频输出，返回 paused 并继续接收恢复事件；恢复后继续音频与物理步进。随后由 Engine 更新根系统，由 GameplaySession 处理显式部署控制和资源切换，最后提交渲染帧。
- 项目场景渲染失败时停止 GameplaySession 并尝试空白渲染；恢复成功继续运行，错误保留在会话中。系统更新或空白渲染失败保持 failed，直到关闭后重新初始化。渲染过程中收到平台暂停时返回 paused。
- `gameplay()` 返回可空借用，仅在本宿主初始化成功至关闭前有效。宿主调用者必须先关闭借用会话的 EditorSystem，再销毁 RuntimeHost。
- `world()` 返回同生命周期的可空世界借用，供宿主连接独立编辑器业务；运行时不认识命令服务或 MCP。
- `window()` 返回同生命周期的可空窗口借用，供调用方独立组织事件处理和渲染帧；核心不判断调用方模式。
- `renderer()` 返回同生命周期的可空渲染器借用，宿主据此装配编辑器渲染命令；截图和渲染设置接口由 RenderSystem 提供。
- 关闭先销毁玩法会话，再关闭 Engine 拥有的渲染、世界及可选资源根系统，最后销毁窗口。借用的外部 ResourceSystem 由调用宿主继续管理，本对象不关闭它。部分初始化和重复关闭均安全。
- RuntimeHost 不解析项目目录、不读取编辑器配置，也不实现脚本热重载；这些职责分别属于编辑器、平台路径和脚本模块。

可执行宿主负责按构建清单装配 ResourceGraphSystem 并向资源底层注入 ResourceLoader；RuntimeHost 库不 include 或链接具体资源插件。外部资源有效期覆盖本宿主及全部领域使用者，关闭消费者后先解除加载器借用，再关闭插件与资源底层。

`RuntimeSetup = bool (*)(RuntimeStage, WorldSystem&, WindowSystem&, PhysicsSystem&)` 是宿主回调别名。
对象先构造，世界尚未初始化时分别以 `before_physics` 和 `after_physics` 调用一次；窗口已就绪，
物理实例已存在，但初始化前不得使用其运行能力。回调只追加对应位置的可选子系统，不在每帧调用。
可选组件由附属系统的 `on_initialize()` 登记，避免被世界初始化时的基础组件表替换。
返回 false 则按现有生命周期清理；核心不解析插件名，也不执行插件策略。

世界子系统的登记、初始化和更新顺序统一如下，关闭时反序执行：

```mermaid
flowchart TD
    F[FontSystem] --> U[UiSystem]
    U --> N[NavigationSystem]
    N --> S[可选 ScriptRuntime：脚本与 UI 业务意图]
    S --> B[before_physics：宿主追加玩家输入系统]
    B --> P[PhysicsSystem：同帧消费移动意图]
    P --> A[AnimationSystem：消费物理后的状态]
    A --> C[after_physics：宿主追加相机跟随系统]
    C --> O[AudioSystem：使用最终空间位置]
```

字体与 UI 先初始化，脚本和插件均借用已构造的对象；整个世界初始化成功后才允许玩法与逐帧调用。
具体玩家控制器和相机插件只由可执行宿主装配，RuntimeHost 不包含插件头文件。
阶段枚举仅决定一次性的子系统登记位置，不增加 System 的逐帧阶段、事件总线或额外调度器。
