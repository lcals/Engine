# 脚本系统

[类图索引](../../../class-diagrams/index.md) · [玩法会话](../../../runtime/class-diagrams/gameplay-session.md) · [脚本绑定](script-bindings.md)

ScriptSystem 属于 `plugins/runtime/script/`，实现核心 [ScriptRuntime](../../../runtime/class-diagrams/script-runtime.md) 契约并通过 [ScriptVm](script-vm.md) 管理模块预检与执行、玩法回调和脚本运行资源；资源部署选择及清单切换由 GameplaySession 调用。

C++ 类型为 `engine::plugins::script::ScriptSystem`；核心契约和借用对象保留在 `engine`。宿主直接引用插件命名空间，核心不提供插件类型别名。

```mermaid
classDiagram
    class ProjectScriptRequest
    ScriptSystem ..> ProjectScriptRequest : 同步调用期间借用
    class ScriptSystem {
        <<final>>
        +ScriptSystem(WorldSystem#42; world, RenderSystem#42; renderer, UiSystem#42; ui, PhysicsSystem#42; physics, NavigationSystem#42; navigation, InputSystem#42; input, WindowSystem#42; window, AnimationSystem#42; animation)
        +~ScriptSystem()
        +prepare_gameplay(const ResourceCatalog& catalog, const MountList& mounts, string_view entry, string_view revision) bool
        +start_prepared_gameplay() bool
        +stop_gameplay() void
        +execute_project_script(const ProjectScriptRequest& request, string& result_json, string& error) bool
        +status() const ScriptStatus&
        #on_initialize(const EngineConfig& config) bool
        #on_tick(double delta_time) bool
        #on_shutdown() void
    }
    class ScriptStatus
    class ScriptRuntime
    class WorldSystem
    class RenderSystem
    class UiSystem
    class NavigationSystem
    class InputSystem
    class WindowSystem
    class AnimationSystem
    class AnimationEvent
    class ResourceCatalog
    class ResourceMount
    class Resource
    class ResourceSystem
    class ResourceLoader
    ScriptRuntime <|-- ScriptSystem
    WorldSystem *-- ScriptSystem : 可选子系统
    ScriptSystem --> WorldSystem : 可空借用世界
    ScriptSystem --> RenderSystem : 可空借用，仅供有效玩法回调
    ScriptSystem --> UiSystem : 可空借用，主线程控件事件
    ScriptSystem --> NavigationSystem : 可空借用，覆盖玩法及场景清理
    ScriptSystem --> InputSystem : 可空借用，覆盖Run句柄清理
    ScriptSystem --> WindowSystem : 可空借用，覆盖Run鼠标清理
    ScriptSystem --> AnimationSystem : 可空借用，覆盖事件接收器解绑
    ScriptSystem ..> AnimationEvent : 姿态提交后接收值批次
    ScriptSystem *-- ScriptStatus : 状态
    ScriptSystem ..> ResourceCatalog : 复制候选元数据
    ScriptSystem ..> ResourceMount : 候选存储输入
    ScriptSystem ..> ResourceSystem : 静态读取候选与目录查询
    ScriptSystem ..> ResourceLoader : 获取活动脚本资源
    ScriptSystem o-- Resource : 活动脚本共享引用
```

```mermaid
classDiagram
    class ScriptSystem
    class ScriptBindingRegistry
    class BindingContext
    class Gameplay
    class Scene
    class EngineConfig
    class ScriptVm
    ScriptSystem *-- ScriptBindingRegistry : 独占绑定注册表
    ScriptSystem *-- BindingContext : 每次运行独占
    ScriptVm *-- Gameplay : 每次运行入口
    ScriptSystem ..> Scene : 绑定创建及退出清理
    ScriptSystem ..> EngineConfig : 初始化参数
    ScriptSystem *-- ScriptVm : 每个候选及活动Run独占
```

- `world`、`renderer`、`ui`、`physics`、`navigation`、`input`、`window` 和 `animation` 默认空指针；非空世界必须比本系统存活更久。服务借用覆盖全部玩法回调，navigation 还须覆盖停止时的场景清理，input 须覆盖 Run 的通道与捕获清理；调用方先停止玩法再关闭这些系统。停止后上下文清理不访问渲染器。初始化准备绑定注册表，并创建临时 VM 验证所选后端能够初始化，随后释放；不启动项目，也不读取资源部署文件。

`animation` 是独立可空借用，不改变 ScriptRuntime 核心接口。初始化时在既有 AnimationSystem 安装单一事件接收器；原生姿态及挂点提交后，仅在主线程非重入安全点将完整事件批次放入活动 Run 的 BindingContext，再由现有 `call_binding` 在 begin_call/end_call 范围调用 `__dispatchAnimationEvents`。队列失败或脚本回调失败返回 false，不截断后报告成功；没有活动 Run 时不存在本 Run 的订阅。关闭先解绑接收器再清理 Run/VM；借用对象须至少存活至解绑完成。状态机由业务 Update 后的内部绑定入口自动驱动，暂停菜单保留正常 Update。

构造和宿主脚本工厂最后追加窗口借用。ScriptSystem 的 window 默认 nullptr，非空时
必须覆盖全部 Run 与鼠标状态恢复；只传给既有 BindingContext/逐类绑定，不在 ScriptSystem
直接调用 SDL。世界暂停时 UI 事件和 Gameplay.Update 继续，项目自行跳过世界业务。
分步资源操作由每 Run 的 BindingContext 独占，不另起加载系统、线程或调度器，停止先取消操作，
再按已有顺序清理绑定、VM、场景和共享引用。详见[运行时控制](runtime-controls-script-api.md)。
- `prepare_gameplay()` 接收调用方提供的目录和挂载，入口默认 `scripts/game.js`，修订默认目录修订。只沿静态 import 图读取候选 JS、编译和链接，不执行模块或用户回调、不更新核心目录、不停止旧玩法。失败保留旧运行。
- 宿主预检成功后停止旧玩法及 GPU、更新资源目录，再调用 `start_prepared_gameplay()`。启动核对活动资源与预检脚本的内容及模块身份，取得共享引用，然后执行模块并创建入口。执行失败停止本次玩法，不承诺撤销外部副作用。
- 所有调用方使用同一候选预检与启动路径；测试夹具同样先预检、停止旧玩法、提交目录，再启动候选，不保留仅供测试的直接启动接口或活动目录读取分支。
- 每个 Run 独占 ScriptVm 环境、模块缓存、任务队列、Gameplay、BindingContext 以及实际加载脚本的 ResourcePtr；目录只复制元数据，脚本实体按 import 读取。只接受相对导入与虚拟 `engine` 模块，不支持 Node.js、动态 import 或顶层 await；传递依赖和循环依赖同样执行预检。
- 生命周期 `OnInitialize/Update/OnShutdown` 同步调用；初始化必须返回 true。回调结束后排空该 Run 的普通 Promise jobs，同次 checkpoint 内已处理的拒绝不误停；未处理拒绝、异步生命周期或动态 import 均报告失败。Update 失败停止玩法但仍执行 OnShutdown，Engine 帧仍返回成功。C++ 不使用异常。
- 停止先在场景仍有效时调用 OnShutdown 并清理绑定订阅，再释放虚拟机与任务队列，随后按 Scene ID 精确销毁属于此 Run 的所有场景（包括常驻内容）及脚本引用。部分初始化、重复停止和关闭均安全；候选 Run 不拥有已发布场景，不清理其他运行者的场景。
- 原生包装通过 Run 的 BindingContext 活性、Scene generation、Node id 和调用阶段检查有效性；模块顶层、构造及预检阶段不能修改场景。Time.deltaTime 仅在 Update 有效。
- 热重载许可和可用部署版本由 GameplaySession 持有。ScriptSystem 只报告活动 Run 的执行状态，不轮询 available.json、控制文件或编辑器目录，也不自动激活资源。
- `ENGINE_SCRIPT_BACKEND` 在编译期选择 `quickjs`（默认）或 `v8`，只编译所选后端；不提供运行时切换。QuickJS 的 runtime/context 与 V8 的 isolate/context 都由各 Run 独占，V8 platform 每进程共享。所有平台的 V8 默认关闭 WebAssembly、Intl/ICU、Temporal、系统 instrumentation，保留 JIT，默认无 ICU 数据部署。
- `status()` 返回只读借用，下一次运行操作可更新内容。`entry` 默认空、`revision` 默认空；MountList 为标准容器别名。构造、析构、停止、查询及关闭为 noexcept，返回状态与重要查询标注 nodiscard，钩子标注 override。

- 可执行宿主按插件清单向 RuntimeHost 提供创建工厂；工厂接收 WorldSystem、RenderSystem、UiSystem、PhysicsSystem、NavigationSystem 和 InputSystem 借用，返回未初始化实例。世界独占插件并统一调度；核心只包含 ScriptRuntime/ScriptStatus 头文件，不链接具体虚拟机或本插件。RuntimeHost 在关闭渲染器前通过 GameplaySession 停止玩法。

宿主注入可空 `PhysicsSystem*`，其有效期覆盖玩法。绑定仅在有效回调中访问本 Run 存活对象的只读撞击快照，包含常驻对象；不推进物理、不在 Jolt 回调里执行脚本。

```mermaid
classDiagram
class ScriptSystem
class BindingContext
class PhysicsSystem
class PhysicsBinding
class ScriptBinding
ScriptSystem --> PhysicsSystem : borrowed
BindingContext --> PhysicsSystem : borrowed during callbacks
ScriptBinding <|-- PhysicsBinding
PhysicsBinding ..> PhysicsSystem : reads impact snapshot
PhysicsBinding ..> BindingContext : validates scene
```

宿主注入可空 NavigationSystem 借用，接口见[导航脚本类图](navigation-script-api.md)。BindingContext 在自己拥有的场景切换/停止时 clear 导航；候选 Run 预检不修改活动导航。导航仅接受调用方明确提交的网格与范围，失败路径不能继续使用受影响旧 tile。
