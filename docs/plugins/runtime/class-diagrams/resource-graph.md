# 资源生命周期插件

[类图索引](../../../class-diagrams/index.md) · [统一生命周期](../../../runtime/class-diagrams/resource-lifecycle.md) · [高层接口](../../../runtime/class-diagrams/resource-loader.md) · [核心读取](../../../runtime/class-diagrams/resource-system.md)

**状态：统一资源契约及业务接入补充已按确认类图实现；验证范围见测试文档。** 保留 plugins/runtime/resource_graph/ 的归属。插件是对底层能力的高级封装：组织加载、依赖、共享、内部计数和卸载；核心不承载这些策略，业务不直接组合单项读取。

**2026-11-07 大场景变更已实现：** 本页配置及内部组织调整已实现，本批验证范围见测试文档。公开加载 / 引用语义保持；闭包路径获取的变更见 ResourceLoader，跨模块入口见[大场景准备](../../../runtime/class-diagrams/large-scene-preparation.md)。

默认实现为 engine::plugins::resource_graph::ResourceGraphSystem。ResourceLoader 等契约在核心 engine 命名空间，业务只依赖它们；其他人可提供符合相同契约的加载器，由可执行宿主替换注入。

## 默认包装实现

```mermaid
classDiagram
    class ResourceGraphSystem {
        <<final>>
        +ResourceGraphSystem(ResourceSystem& resources, TaskSystem& tasks, const ResourceGraphConfig& config)
        +~ResourceGraphSystem()
        #on_initialize(const EngineConfig& config) bool
        #on_tick(double delta_time) bool
        #on_shutdown() void
    }
    class ResourceGraphConfig {
        <<struct>>
        +uint32_t max_requests = 128
        +uint32_t completion_budget = 16
        +double coordination_budget_seconds = 0.002
    }
    class System
    class ResourceLoader
    class ResourceSystem
    class TaskSystem
    class ResourceRead
    class ResourceRecord
    class GraphResourceLoad {
        <<private final>>
    }
    class ResourceLoad
    class EngineConfig
    System <|-- ResourceGraphSystem
    ResourceLoader <|-- ResourceGraphSystem
    ResourceLoad <|-- GraphResourceLoad
    ResourceGraphSystem --> ResourceSystem : 借用核心读取服务 组合包装
    ResourceGraphSystem --> TaskSystem : 借用独立资源组织任务域
    ResourceGraphSystem *-- ResourceGraphConfig
    ResourceGraphSystem *-- ResourceRecord : 就绪及在途记录
    ResourceGraphSystem ..> ResourceRead : 创建并交由在途记录独占
    ResourceGraphSystem ..> GraphResourceLoad : 创建后交由业务独占
    ResourceGraphSystem ..> EngineConfig
```

全部业务公开方法继承并 override [ResourceLoader 契约](../../../runtime/class-diagrams/resource-loader.md#加载接口)，不在此重复维护签名。构造只保存必要借用和配置；初始化检查服务就绪和预算，失败返回。析构/关闭 noexcept，禁止复制和移动。资源核心和任务域必须先初始化，并存活到插件关闭完成。

包装采用组合，不继承 ResourceSystem 或 VirtualFileSystem，不把插件内部对象暴露给业务。插件不 include 编辑器、项目策略或具体领域处理器；类型扩展经核心登记表接入。宿主只在装配处知道默认实现。

## 记录、获取和在途合并

**本节大场景增量已实现。** 已删除的 task_references 镜像字段不恢复；后台任务安全由实际工作状态或独立资源获取的所有权保证。

```mermaid
classDiagram
    class ResourceGraphSystem
    class ResourceRecord {
        <<private struct>>
        +unique_ptr~Resource~ value
        +unique_ptr~Resource~ candidate
        +unique_ptr~ResourceRead~ read
        +uint64_t acquisitions
        +uint64_t dependency_references
        +uint64_t request_references
        +uint32_t unresolved_dependencies
    }
    class Acquisition {
        <<private struct>>
    }
    class LoadState {
        <<private struct>>
        +uint32_t completed
        +uint32_t total
        +size_t admission_cursor
    }
    class LoadPlan {
        <<private immutable struct>>
    }
    class DependencyIndex {
        <<private immutable struct>>
    }
    class GraphResourceLoad
    class Resource
    class ResourceRead
    class ResourceCatalog
    ResourceGraphSystem *-- ResourceRecord : 按目录代际和GUID索引
    ResourceGraphSystem *-- Acquisition : 每次获取独立身份
    ResourceGraphSystem o-- LoadState : 活动请求状态
    GraphResourceLoad o-- LoadState : 仅共享控制状态 不裸借用插件
    Acquisition --> ResourceRecord : 一次获取引用
    LoadState --> ResourceRecord : 本请求候选或未领取根引用
    ResourceRecord *-- Resource : 只读数据独占所有权
    ResourceRecord *-- ResourceRead : 尚未完成时拥有操作
    ResourceRecord --> ResourceRecord : 去重后的直接依赖引用
    ResourceRecord o-- DependencyIndex : 已建立的根闭包身份索引
    LoadState o-- LoadPlan : 共享去重计划 不复制载荷
    LoadPlan *-- ResourceCatalog : 目录值共享不可变快照
    LoadPlan o-- DependencyIndex : 根的GUID与路径成员索引
    ResourceRecord ..> LoadState : 以请求身份通知一次完成
```

这些是插件内部记录示意，不是新增公开架构层。计数区分获取、依赖、请求，所有加减在协调线程完成；业务模块任务通过独立获取计入 acquisitions，不再维护只反映 read 是否存在的 task_references。计数溢出/身份耗尽返回明确错误，不回绕。共享控制状态允许请求晚于插件析构，资源引用仍由插件集中核算。

- 索引键固定为目录代际 + GUID，同代际路径由目录唯一映射；同一在途资源只存在一份读取/解码。多个请求只是增加各自等待关系，不各自保存一棵重复加载树。
- 每个请求从必要依赖声明建立去重后序计划并检测环。只处理真实必要依赖，不按文件分组加载未使用的模型子资源。
- 计划共享该目录代际的不可变 ResourceCatalog；同代际、同根、同内存定义集合的在途请求共享一份计划。内存定义用独立不可变元数据保活，不能让 worker 借用可能被撤销的 ResourceRecord。
- DependencyIndex 按 GUID 和逻辑路径记录根的必要闭包成员，只保存身份 / 元数据索引，不增加第二套载荷引用。请求根在计划阶段建立索引；未独立规划过的依赖根仅在首次需要其闭包查询时建立并缓存一次，成本归本次获取，不能藏到 status/get 中。不为每个资源预先复制所有祖先的传递闭包，避免索引内存平方增长。
- 每个请求用去重集合登记引用；每条依赖边只计数一次。记录发布时通过反向等待边递减父项的 unresolved_dependencies，并向订阅请求增加 completed；重复通知、已取消身份和已完成请求被拒绝。禁止每帧对全部记录逐个扫描全部请求，再在线性 pins 数组里找成员；status 读取已维护计数。
- 完整实例身份仍经加载器校验。闭包索引不会让旧获取复活；撤销记录或最后拥有者退出时销毁其索引。动态组件绑定仍从当前已加载目录获取；恢复作用域使用根闭包，两者不混代。
- 每项先得到核心单项候选，再在直接依赖就绪后调用核心 validate_dependencies；成功才原子发布该项及其依赖关系。根 ready 前完整闭包必须就绪。
- 原子单位是一个资源记录及其已就绪闭包；已成功的共享依赖可服务其他请求。根失败只回滚本请求持有的临时引用；无其他引用的依赖立即卸载，不清除其他请求已取得的数据。
- 同一资源多次成功 load 得到独立 Acquisition，数据仍只有一份。release 只撤销自己的身份；不允许 unload(guid) 强制破坏别人的使用。
- 三种记录引用归零后撤出可加载索引，取消并释放本记录的 ResourceRead，再销毁本项及释放依赖边。核心单项读取任务只捕获自身共享工作状态、独立输入与输出，不借用 ResourceRecord；取消后尚未结束的工作继续由核心持有并回收，不能回写已撤出的记录。关闭核心必须回收工作后再销毁处理器和任务域。无常驻引用的资源不能仅因索引存在而滞留。
- 模块运行实例自行 retain，任务保活由实例或独立获取负责；业务释放原始句柄不会终止别人的播放或渲染。GPU、VM、声部和 ECS 实例销毁仍由对应模块负责。

## 同步与异步执行

```mermaid
sequenceDiagram
    participant B as 业务
    participant P as ResourceGraphSystem
    participant C as 核心 ResourceSystem
    participant F as VirtualFileSystem
    B->>P: begin_load(path)
    P-->>B: 独占 ResourceLoad
    P->>P: 快照与去重计划 合并在途记录
    P->>C: begin_read(guid) 每项一次
    C->>F: begin_read(location, range)
    F-->>C: ready 字节缓冲
    C->>C: 解析任务与本项校验
    C-->>P: ready 独占 Resource
    P->>C: validate_dependencies(resource, dependencies)
    C-->>P: 校验结果
    P->>P: 发布与引用计数 请求变为 ready
    B->>P: ResourceLoad.take(handle)
    P-->>B: 一次获取身份
    B->>P: release(handle)
    P->>P: 减少本次获取 无引用则反向卸载
```

图中等待跨系统 tick 完成，不是单次 begin_load 中同步执行所有步骤。业务 status 是只读查询。宿主先派发共享任务完成，再按依赖顺序 tick 核心读取系统、插件和使用模块；关闭反序。

异步目录计划等 CPU 工作使用本插件的 TaskSystem；输入为不可变目录快照，worker 不修改共享记录。解析任务属于核心读取域，文件任务属于文件域，互不挪用领域模块的任务队列。插件 on_tick 只做有预算的协调与发布，不等待 worker。

### 执行窗口与预算

- 删除 `max_pending_resources`，不再因一个场景的未就绪闭包超过 1024 项而失败。完整计划只保存元数据；单项读取的数量 / 字节窗口仍由 ResourceReadConfig 与 FileAsyncConfig 独占管理，插件遇到 busy 保留游标并让出，不另建同义额度或线程池。
- 计划落入共享记录、等待边登记、读取提交、发布和取消清理由同一协调时间预算限制，并保留游标；completion_budget 限制每 tick 的终态发布数量，不能把这个数误用为每帧只能遍历同样数量的所有元数据。初始化拒绝零发布预算及非有限 / 非正时间预算。
- 可以在依赖未发布时读取 / 解码本项；只有依赖全部 ready 才调用 validate_dependencies 并发布。优先调度能解除依赖等待的项，窗口空闲时允许后续项预读。候选占用的字节纳入诊断；窗口只约束在途传输 / 解码，不能宣称限制最终就绪资源的总驻留内存。
- 单项本身超过核心读取上限仍明确 budget_exceeded，不偷偷扩大缓冲。超大实体分块 / 资源分区属于独立格式设计；整个包大于窗口不构成单项超限。
- 相同根 / 依赖的请求只共享工作和数据，各自保留独立获取、取消和进度。注册到已 ready 记录时只计一次完成；等待表以可校验请求身份定位，不持有可悬空的请求指针。
- 取消先使请求身份失效，随后分步解除其临时引用；其他请求仍需要的记录继续推进。后台工作结束前保活输入，不能在协调线程等待 future。清理队列不得作为常驻缓存；关闭阶段完整回收这些工作后再关闭借用服务。

同步 load 在协调线程完成自己的计划、读取与校验。若闭包已有在途异步记录，先返回 busy，不等待完成委托、不重复读、不 tick 其他系统。完整命中 ready 时直接获取。异步资源预算暂满时插件有界排队，先检查取消/换代再重试；单项超限明确失败，不无限等待。

取消一个请求不取消其他等待者；无需求的候选才向核心取消。失败原因按请求保存，不因共享记录的另一调用覆盖。目录换代在下一次推进/命令处理前检查，旧未完成请求以 generation_changed 失败；发布前再次核对，晚到结果不得写入新代际。

已完成的旧获取保持完整旧闭包；get/retain/retain_dependency 可以继续访问，新的路径/GUID 加载只使用当前代际。内存入口也采用同样的发布和计数过程，接纳细则见加载契约。

## 装配、替换与关闭

默认插件继续在 cmake/plugins/runtime/resource_graph.cmake 登记；未选择时不创建或链接它的目标。宿主可以装配另一 ResourceLoader 实现，但必须通过相同契约验收；不能让业务根据具体实现类型选择不同流程。

替换实现发生在宿主装配边界：结束旧实现的业务、实例和操作，释放所有获取并解除借用，再关闭旧实现并注入新实现。这里不承诺热插拔或活动句柄跨实现迁移。

关闭先拒绝新请求，取消在途操作并收尾任务；将未交付请求终结、回收未领取获取和全部记录，最后解除核心与任务域借用。遗漏的业务句柄失效并可诊断，不能越过关闭继续使用。核心及任务域随后关闭。操作晚于插件销毁时只访问终结的 LoadState，不访问插件地址。
