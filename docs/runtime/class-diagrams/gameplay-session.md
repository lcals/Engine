# 玩法运行会话

[类图索引](../../class-diagrams/index.md) · [脚本运行契约](script-runtime.md) · [运行宿主](runtime-host.md)

[分阶段启动](movie-startup.md)先提交真实影片首帧，再准备玩法；ready 包含所请求的 VM、ECS 与 GPU。
场景停止只清理场景声部，Native 影片与共享设备由宿主及调用者管理。

```mermaid
classDiagram
    class ProjectScriptRequest
    GameplaySession ..> ProjectScriptRequest : 同步调用期间借用
    class GameplaySession {
        <<final>>
        +GameplaySession(WorldSystem& world, RenderSystem& renderer, ScriptRuntime#42; script, PhysicsSystem#42; physics, AudioSystem#42; audio, AnimationSystem#42; animation, FontSystem#42; fonts, UiSystem#42; ui, TaskSystem#42; background)
        +~GameplaySession()
        +set_resources(const AssetCatalogDataT& data, const MountList& mounts, string_view entry) bool
        +start(string_view entry) bool
        +begin_prepare(const ResourceCatalog& catalog, const MountList& mounts, string_view entry, string_view scene) bool
        +step_prepare(uint32_t resource_budget, double max_seconds) GameplayPrepareStatus
        +activate_prepared() bool
        +tick() bool
        +stop(string_view reason) void
        +execute_project_script(const ProjectScriptRequest& request, string& result_json, string& error) bool
        +script_status() const ScriptStatus#42;
        +last_error() string_view
    }
    class GameplayPrepareStatus {
        <<enumeration>>
        preparing
        ready
        failed
    }
    GameplaySession ..> GameplayPrepareStatus : 分步准备结果
    class WorldSystem
    class RenderSystem
    class ScriptRuntime
    class PhysicsSystem
    class AudioSystem
    class AnimationSystem
    class ResourceSystem
    class ResourceCatalog
    class TaskSystem
    GameplaySession --> TaskSystem : 可空借用后台部署读取与状态发布
    GameplaySession --> WorldSystem : 必需借用
    GameplaySession --> RenderSystem : 必需借用
    GameplaySession --> ScriptRuntime : 可空借用抽象执行契约
    GameplaySession --> PhysicsSystem : 可空借用 清理物理场景
    GameplaySession --> AnimationSystem : 可空借用并清理采样缓存
    GameplaySession --> AudioSystem : 可空借用 只停止并释放场景声部
    GameplaySession --> UiSystem : 可空借用 先释放控件文档
    GameplaySession --> FontSystem : 可空借用 释放字体 GPU 缓存
    GameplaySession ..> ResourceSystem : 显式提交资源目录
    GameplaySession *-- ResourceCatalog : 基线及候选元数据
```

```mermaid
classDiagram
    class GameplaySession
    class AssetCatalogDataT
    class ResourceMount
    class ScriptStatus
    class App
    GameplaySession ..> AssetCatalogDataT : 目录输入
    GameplaySession ..> ResourceMount : 存储挂载输入
    GameplaySession ..> ScriptStatus : 借用脚本执行状态
    GameplaySession ..> App : 宿主部署及控制路径
```

- 会话是调用方协调对象，不继承 System、不拥有其他系统。RuntimeHost 在系统就绪后创建并独占它，先销毁会话再关闭系统。
- 会话只调用 ScriptRuntime 的预检、启动、停止和状态接口，不依赖具体虚拟机或 ScriptSystem 插件。热重载许可与可用部署版本由会话独占；脚本实例为空时仍可关闭场景与渲染，脚本玩法启动返回明确失败。
- `set_resources()` 由编辑器直接提交已导入的完整目录与 content 挂载，入口默认 `scripts/game.js`。运行中拒绝替换；调用方须先停止。未装配脚本时仍可提交资源目录，供原生场景和渲染使用；脚本启动另行报告不可用。无编辑器配置进入资源核心。
- 首次 `start()` 先验证宿主发布的完整持久候选；可用时直接选用，不要求先读内置目录。无可用持久候选时才从 `App::assets_path()` 读取 `catalog.bin`，或采用 `set_resources()` 已提交的完整基线。持久清单读取、格式、文件校验或目录错误保留为会话诊断，不阻止有效基线启动；完整版本按批选择，核心不做逐项回退。以目录修订标识包内版本，入口默认 `scripts/game.js`，调用方可显式覆盖。Android 不再生成或读取额外包内 JSON，也不导出首次同步基线。正常运行不扫描源项目；仅持久候选校验完整发布文件，内置目录不预读全部实体。
- `start()` 已运行时幂等。先预检选定完整版本的脚本，再停止旧玩法、销毁场景与 GPU，提交目录、启动已预检脚本并准备 GPU。候选预检失败保留旧玩法；目录应用或新入口失败保持停止并报告。热重载候选的发布校验失败保留活动版本，不切换到内置版本。
- background 非空时，control、manifest、目录读取和完整文件 hash 由后台任务域执行，保持一个在途候选。任务只接路径和旧标识快照，不访问 App、Scene、VM 或会话对象；完成委托标记私有结果，tick 在所属线程应用。目录替换/启停递增请求代际，旧候选不再应用。
- 状态文本按最新值合并，写入与部署读取共用一个串行后台作业，避免旧状态晚到覆盖新状态。显式 start 保留同步语义；析构关闭可等待后台收尾并发布最后状态。扫描和流式 hash 仍使用同步文件工具，但限为后台域中的单个任务，不占主线程；后续可迁移为原生分块异步 I/O。
- `tick()` 由 RuntimeHost 在系统更新后显式调用，处理 `script-control.json` 与 MCP 发布的 `assets/available.json`。这是调用方部署协议，核心不识别这些文件。完整目录元数据与发布文件校验通过后记录候选；热重载关闭只更新可用状态，不切换。
- 持久部署版本位于 `assets/versions/<revision>`，不可原地覆盖。目录中的位置可指向散文件或包成员；清单与存储集合必须一致。发布文件用固定缓冲区流式校验，不解包整个目录。实体只在脚本预检或核心按需加载时读取；包体尺寸不受单实体上限约束。
- 显式 stop 或 Update 失败后不因新候选而自动重启。重新开启重载允许重试最新候选。普通运行集合固定，只有显式发布部署文件才触发候选处理。
- `stop()` 先退出脚本，再停止音频、清理物理、销毁场景和 GPU；资源随共享引用释放，保留资源索引和内置定义以供下次启动。重复关闭安全。
- `script_status()` 直接返回脚本执行状态的只读借用，不复制或覆盖插件状态；无脚本模块返回空。会话发布 `script-status.json` 时组合执行状态、自身热重载许可及可用版本；部署错误通过会话 `last_error()` 报告，发布时优先于执行错误。
- 类型不可复制或移动。构造、析构、stop 与查询为 noexcept；查询为 const，可能失败方法与重要查询标注 nodiscard。entry/reason 默认空。
