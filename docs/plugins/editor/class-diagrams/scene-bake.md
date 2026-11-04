# 场景 GPU 烘焙

本页维护 IBL 与静态照明共用的任务和发布契约；Lightmap 与光照探针的现行接口见[静态照明烘焙](scene-lighting-bake.md)。

```mermaid
classDiagram
    class SceneBakeSystem {
        +SceneBakeSystem(ResourceSystem& resources, RenderSystem& renderer, TaskSystem& tasks, WorldSystem& world, Path project_root, ValidateCallback validate, PublishCallback publish)
        +register_commands(EditorCommands& commands) bool
        +busy() bool
        #on_initialize(const EngineConfig& config) bool
        #on_tick(double delta_time) bool
        #on_shutdown() void
    }
    class IblBakeJob
    class LightingBakeJob
    class SceneBakeFiles
    class register_scene_bake_tools {
        <<namespace function>>
        +register_scene_bake_tools(McpSystem& system, EditorMcpTools& editor) bool
    }
    class System
    class RenderSystem
    class ResourceSystem
    class TaskSystem
    class WorldSystem
    class EditorCommands
    class EditorMcpTools
    class McpSystem
    System <|-- SceneBakeSystem
    SceneBakeSystem *-- IblBakeJob : State 独占 IBL 任务及读回缓冲
    SceneBakeSystem *-- LightingBakeJob : State 独占照明任务，与 IBL 互斥
    SceneBakeSystem *-- SceneBakeFiles : State 按值拥有文件发布事务
    SceneBakeSystem --> RenderSystem : 借用 GPU 就绪和已提交帧号
    SceneBakeSystem --> WorldSystem : 借用当前预览场景并解析持久节点身份
    SceneBakeSystem --> TaskSystem : 借用编辑器任务域准备照明数据
    SceneBakeSystem --> ResourceSystem : 加载源纹理及校验输入版本
    SceneBakeSystem ..> EditorCommands : 注册启动查询取消命令
    register_scene_bake_tools --> EditorMcpTools : 转发编辑器命令
    register_scene_bake_tools ..> McpSystem : 登记工具
```

`SceneBakeSystem` 通过私有 `State` 持有 GPU 任务、文件事务、输入 ResourcePtr、任务身份和状态；ValidateCallback / PublishCallback 由宿主提供且依赖对象覆盖系统生命周期。ValidateCallback 输出当前场景身份并检查已保存预览，PublishCallback 只执行通用导入刷新。注册命令与 MCP 回调均为借用，必须先关闭命令服务再销毁模块。

资源加载通过 `ResourceSystem::loader()` 的核心契约，不引用具体 `ResourceGraphSystem`。默认单项加载器可直接读取当前目录挂载中的输入；IBL 输入引用由 State 持有，几何、材质、Shader 与纹理依赖由场景组件及材质持有，GPU 捕获复用其已有引用。

```mermaid
classDiagram
    class IblBakeJob {
        +IblBakeJob()
        +start(const TextureDataT& source, uint16 resolution, uint16 samples, uint16 lut_size, string& error) bool
        +tick(uint32 frame, string& error) BakeProgress
        +cancel() void
        +shutdown() void
        +completed() uint32
        +total() uint32
        +outputs() const KtxBytes&
    }
    class BakeProgress {
        <<enumeration>>
        running
        complete
        cancelled
        failed
    }
    class TextureDataT
    IblBakeJob ..> TextureDataT : start 只读输入生成数据
    IblBakeJob ..> BakeProgress : 每次推进返回状态
```

`outputs()` 返回 `const std::array<std::vector<std::uint8_t>, 3>&`，按 irradiance、prefilter、LUT 顺序借用完成的 KTX 字节；只在 complete 后消费，借用有效期截至任务销毁。`completed()` / `total()` 是已完成和全部 GPU 面/mip 工作项数量，均 const、noexcept。取消和关闭为 noexcept；本类不持有源场景，不创建资源 GUID、不执行资源导入。

```mermaid
classDiagram
    class SceneBakeFiles {
        +initialize(const filesystem_path& root, string_view scene, string_view input, string_view job, ResourceSystem& resources, string& error) bool
        +initialize_lighting(const filesystem_path& root, string_view scene, string_view job, ResourceSystem& resources, LightingBakeSettings& settings, string& error) bool
        +stage_lighting(const LightingBakeOutputs& output, string& error) bool
        +unchanged(ResourceSystem& resources, string& error) bool
        +stage(const KtxBytes& images, string& error) bool
        +publish(const RefreshCallback& refresh, string& error) bool
        +outputs() const vector~BakeOutput~&
        +cleanup() void
    }
    class BakeOutput {
        +string path
        +string guid
    }
    class ResourceSystem
    class SceneImporter
    class LightingBakeSettings
    class LightingBakeOutputs
    class SceneDataT
    class ProjectLock
    SceneBakeFiles *-- BakeOutput : 本次 IBL 或场景照明输出路径与稳定 GUID
    SceneBakeFiles ..> LightingBakeSettings : 输入与版本指纹
    SceneBakeFiles ..> LightingBakeOutputs : 未发布照明资源
    SceneBakeFiles ..> ResourceSystem : 校验目录版本和读取源文件
    SceneBakeFiles ..> SceneImporter : 解码及保存源场景
    SceneBakeFiles ..> SceneDataT : 修改生成模型的 IBL 引用
    SceneBakeFiles ..> ProjectLock : 发布与回滚时持有项目锁
```

图中 `KtxBytes` 表示 `std::array<std::vector<std::uint8_t>, 3>`，`RefreshCallback` 表示 `std::function<bool(std::string&)>`，仅缩短类图展示，不新增代码类型。`outputs()` 返回 `const std::vector<BakeOutput>&`；`unchanged()` 为 const，`outputs()` 为 const、noexcept，`cleanup()` 为 noexcept。SceneBakeFiles 不拥有资源系统，临时文件、旧文件备份及回滚状态由它管理；导入刷新前先释放项目锁，刷新失败时恢复旧源文件并再次刷新旧目录。失败回滚尚未收回的备份保留以供恢复。

插件属于 `plugins/editor/scene_bake`，运行时核心不包含或链接它。第一类烘焙是 IBL；计算沿用 Ant 的余弦重要性采样、GGX 预过滤和 split-sum BRDF LUT，并保留来源与许可。GPU 调度仅在主线程进行，每帧推进有限计算和读取，不创建后台 bgfx 线程，不自行调用 bgfx::frame。

`bake_scene` 返回已接受任务，`get_scene_bake` 查询状态，只有 complete 表示三个 KTX、meta、场景引用和导入刷新全部完成；`cancel_scene_bake` 请求取消，在途 readTexture 完成前保持缓冲存活。启动和发布前通过宿主检查精确场景路径、场景身份、未修改状态，并比对磁盘场景及输入纹理版本。发布采用 `cache/bake/<job>/` 暂存、旧 GUID 复用、失败回滚，目标为源场景旁同名目录中的 IBL 纹理。烘焙只填写资源引用，保留组件 enabled、场景 ibl_enabled 与强度。优先写入唯一启用的 EnvironmentComponent；没有启用项时，复用唯一停用项并保持停用，多个停用项返回目标歧义错误。只有完全没有环境组件时才创建普通环境节点，IBL 仍默认关闭。

输出：RGBA16F irradiance cubemap、RGBA16F prefilter cubemap 完整 mip 链、RG16F BRDF LUT，KTX1，linear。每面每 mip 先 blit 至独立 2D READ_BACK 纹理，再等待 readTexture 返回帧号，兼容 Metal 的立方纹理读取限制。GPU 计算与读回统一使用 RGBA16F；LUT 读回后仅按字节保留 RG 通道，保存为 RG16F，不在 CPU 重做积分。

关闭顺序：先停止本系统的新提交并释放 GPU 句柄，保留 State 的 CPU 读回缓冲；宿主关闭渲染器并完成 bgfx 队列处理，最后销毁本系统。普通取消同样等在途读回结束再释放内存。

## 后续优化：全局共享 BRDF LUT

当前保留按场景输出三份 IBL 纹理的实现，先保证场景效果。后续将 BRDF LUT 改为引擎内置的全局共享资源，所有场景复用同一份，避免重复烘焙、保存和打包。

BRDF LUT 取决于所选 BRDF 模型与积分约定，与具体场景及天空图无关；漫反射环境图和预滤波反射图仍由各场景烘焙。迁移时同步调整场景资源引用、依赖清单与 Shader 绑定；上述优化尚未实施。

`register_scene_bake_tools` 是模块登记函数，没有适配器对象或独立状态；工具回调直接借用 EditorMcpTools，宿主保证通道存活到 McpSystem 关闭。
