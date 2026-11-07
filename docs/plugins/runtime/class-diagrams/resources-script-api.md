# 资源脚本接口

[类图索引](../../../class-diagrams/index.md) · [资源加载契约](../../../runtime/class-diagrams/resource-loader.md) · [已确认的业务接入](../../../runtime/class-diagrams/resource-consumers.md)

脚本接口直接使用宿主注入的 ResourceLoader。Run 持有每次获取和异步操作，不依赖默认资源插件；实现替换不改变业务流程。

```mermaid
classDiagram
    class Resources {
        <<JavaScript facade>>
        +Load(string path_or_guid) ResourceHandle?$
        +BeginLoad(string path_or_guid) ResourceRequest?$
        +Retain(ResourceHandle handle) ResourceHandle?$
        +Release(ResourceHandle handle) bool$
        +lastError string$
    }
    class ResourceHandle {
        <<opaque JavaScript value>>
    }
    class ResourceRequest {
        +Status() ResourceLoadStatus
        +Progress() ResourceProgress
        +Take() ResourceHandle?
        +Cancel() bool
        +Error() string
    }
    class ResourceProgress {
        +number completed
        +number total
    }
    class ResourceLoadStatus {
        <<enumeration>>
        loading
        ready
        delivered
        failed
        cancelled
    }
    class ResourcesBinding
    class ScriptBinding
    class ScriptBindingRegistry
    class BindingContext
    class ResourceLoader
    class ResourceLoad
    ScriptBinding <|-- ResourcesBinding
    ScriptBindingRegistry *-- ResourcesBinding
    ResourcesBinding ..> BindingContext : 当前 Run 和回调许可
    BindingContext --> ResourceLoader : 可空借用
    BindingContext *-- ResourceLoad : 独占未领取操作
    BindingContext ..> ResourceHandle : 原生获取与不透明令牌对应
    Resources ..> ResourceHandle
    Resources ..> ResourceRequest
    ResourceRequest ..> ResourceLoadStatus
    ResourceRequest ..> ResourceProgress
```

句柄是不可伪造的不透明对象。JavaScript 变量赋值不加引用计数，只有 Load、Retain 和成功 Take 创建独立获取；Release 撤销相应获取。句柄不能跨 Run 使用。

Status、Progress、Error 只查询；资源核心与插件由宿主 tick 推进。Take 仅成功一次，Cancel 不等待任务；Run 关闭取消操作并释放所有尚未交还的获取。新目录不使旧的已就绪获取失效，未完成请求按加载器契约终结。

场景先完成资源加载，再调用 Scene.Load 或 Scene.BeginLoad/StepLoad/CommitLoad；影片把已加载句柄传给 Movie.Play；JSON 使用 Json.ParseResource。使用方会为自身的持续使用独立保活，业务可以在绑定成功后释放原始获取。
