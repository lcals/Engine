# 渲染系统

[类图索引](../../class-diagrams/index.md) · [引擎与系统](engine-system.md) · [完整引用关系](../../class-diagrams/references.md)

本图只定义 `RenderSystem` 的方法，其他节点为跨图引用或第三方库。

```mermaid
classDiagram
    direction TB
    class RenderSystem {
        <<final>>
        +RenderSystem(WindowSystem& windows, WorldSystem#42; world)
        +~RenderSystem()
        +prepare_scene() bool
        +release_scene() void
        +scene_prepared() bool
        +gpu_resource_count() uint32_t
        +set_settings(const RenderSettings& settings) bool
        +settings() const RenderSettings&
        +request_screenshot(string_view utf8_png_path) bool
        +screenshot_status() ScreenshotStatus
        +render() bool
        #on_initialize(const EngineConfig& config) bool
        #on_shutdown() void
    }
    class System {
        <<abstract>>
    }
    class WindowSystem
    class WorldSystem
    class Scene
    class Camera
    class Light
    class MeshRenderer
    class Transform
    class Material
    class ResourceSystem
    class MeshDataT
    class ShaderDataT
    class TextureDataT
    class Resource
    class MeshUtils
    class EngineConfig {
        <<struct>>
    }
    class RenderSettings {
        <<struct>>
        +ambient_color : float[3]
        +ambient_intensity : float
        +exposure : float
        +filmic_tonemap : bool
        +directional_shadows : bool
        +shadow_resolution : uint16_t
        +shadow_bias : float
        +clear_color : uint32_t
    }
    class ScreenshotStatus {
        <<enumeration>>
        idle
        pending
        complete
        failed
    }
    class bgfx {
        <<library>>
    }
    class SDL3 {
        <<library>>
    }
    class ANativeWindow {
        <<external>>
    }
    class SDL_MetalView {
        <<external>>
    }
    System <|-- RenderSystem
    RenderSystem --> WindowSystem : 构造参数及持续借用
    RenderSystem --> WorldSystem : 可空借用 活动场景来源
    RenderSystem ..> Scene : 借用节点列表和唯一场景编号
    RenderSystem ..> Camera : 从节点读取视角与投影
    RenderSystem ..> Light : 从节点读取方向与光照
    RenderSystem ..> MeshRenderer : 从节点读取 Mesh 与材质
    RenderSystem ..> Transform : 从节点读取世界变换
    RenderSystem ..> Material : 读取实例 Shader 路径与命名参数
    RenderSystem ..> ResourceSystem : 内置资源注册及身份查询
    RenderSystem ..> Material : 借用其当前Shader数据
    RenderSystem ..> MeshDataT : 读取组件持有几何并组装 GPU 顶点
    RenderSystem ..> MeshUtils : 几何校验与切线生成
    RenderSystem o-- Resource : 在用 GPU 纹理持有 CPU 引用
    RenderSystem ..> TextureDataT : Basis 转码与 mip 上传
    RenderSystem ..> ShaderDataT : 读取生成的二进制与 uniform 映射
    RenderSystem ..> EngineConfig : on_initialize 参数
    RenderSystem *-- RenderSettings : 拥有渲染配置
    RenderSystem ..> ScreenshotStatus : 截图回执
    RenderSystem ..> bgfx : 图形资源及帧提交
    RenderSystem ..> SDL3 : 原生窗口及像素尺寸
    RenderSystem "1" o-- "0..1" ANativeWindow : Android 持有原生引用
    RenderSystem "1" *-- "0..1" SDL_MetalView : Apple 平台拥有视图
    note for RenderSystem "拥有 bgfx 创建的资源；析构中调用 shutdown()"
```

- 构造时借用窗口系统，以及可空的世界系统；两者必须比渲染系统存活更久。世界系统为空时只显示空白。初始化时检查窗口已就绪，不创建项目 Mesh、Shader 和材质资源。
- `prepare_scene()` 在主线程按当前场景引用的独立 Mesh 资源准备顶点和索引缓冲，并按材质引用的 Shader 路径创建程序，失败返回 `false` 并清理部分初始化。无场景或无可绘制组件时释放已有项目 GPU 资源并成功返回。
- `release_scene()` 释放本次场景的全部 GPU 句柄并清除场景编号，可以重复调用；保持共享 bgfx 设备和窗口。随后 `render()` 在无场景时提交空白以覆盖上一帧。
- `scene_prepared()` 与 `gpu_resource_count()` 为只读运行状态；资源数是本系统当前拥有的有效逻辑句柄数，不代表驱动端已经完成异步回收。切换场景前释放旧资源，bgfx 按帧安全完成实际销毁。
- `render()` 按场景唯一编号识别热重载造成的场景替换；场景节点由 `Scene` 管理，渲染器不缓存组件借用指针。相机、灯光、方块均从 `Node + Component` 读取，不在渲染器内创建隐藏场景对象。
- 相机和灯光以节点局部 `-Z` 为前向；节点世界矩阵使用列主序。相机从世界 `-Z` 和 `+Y` 正交化构建视图，忽略缩放大小，避免节点非均匀缩放扭曲投影。法线使用模型矩阵的逆转置，支持非均匀缩放。
- Android 通过 SDL 窗口属性取得原生窗口，调用 `ANativeWindow_acquire()` 持有原生引用，bgfx 完全关闭后配对 `ANativeWindow_release()`；底层窗口由平台共享，不由渲染系统独占。
- Android 宿主在 Activity 暂停及 Surface 销毁前同步请求 SDL 主线程关闭 bgfx，并等待后台渲染线程退出；确认后才允许 SDL 释放 Surface。宿主不直接调用 bgfx，具体协议见 [Android 宿主](android-host.md)。
- Android 暂停期间保留系统对象，GPU 初始化可推迟；Activity 与 Surface 均就绪后按需重建场景所需的渲染资源。系统生命周期就绪与 GPU 暂时关闭是两个不同状态，公共系统接口不变。
- 初始化、继承的更新、`render()` 和关闭均由 `SDL_main` 所在线程（见[线程约定](engine-system.md)）调用；窗口指针及原生句柄借用不跨线程传递。
- 继承 `System` 的公共生命周期入口及默认 `on_tick()`；它管理的子系统随统一入口更新。
- `render()` 在本帧系统及子系统更新完成后检查窗口尺寸和表面状态，完成图形资源适配及一帧提交。成功返回 `true`，无法完成返回 `false`。若宿主暂停在本帧中途到达，RuntimeHost 重新检查窗口可绘制性并返回 `paused`；项目场景渲染失败时，RuntimeHost 通过 GameplaySession 停止玩法并尝试恢复空白，无法恢复时进入宿主失败状态。
- RuntimeHost 在提交前检查窗口退出和暂停状态。恢复和表面重建由渲染系统与[窗口系统](window-system.md)协作处理。
- 拥有 bgfx 创建的资源，使用对应 API 释放；不拥有窗口系统，也不拥有第三方库本身。初始化、清理和复制移动限制遵守[系统约定](engine-system.md)。

- 使用 SDL3 提供原生窗口句柄，macOS / iOS 选择 Metal、Windows 选择 Direct3D12、Android / Linux 选择 Vulkan；拒绝 Noop 和回退到其他图形后端。待机清屏，无调试文字。窗口像素尺寸变化时重置交换链并更新相机宽高比。
- 默认内置 Mesh 为 24 顶点、36 索引方块；项目 Mesh 来自独立缓存定义，可提供其他顶点和 uint32 索引。内置 PBR/无光照 Shader 的程序数据由固定版本 bgfx `shaderc` 编译并交给 ResourceSystem 注册。材质按 `builtin/...` 或 `project/...` 路径取得资源，渲染器通过 `Material::shader_data()` 借用该材质实际持有的 ShaderData，不经过全局缓存查询或按材质路径分支选择效果。
- 每个在用 Shader 路径拥有一套 Program 和参数 Uniform；多个组件可使用不同 Shader。按生成数据中的 uniform 名称、偏移及数值类型映射参数，上传实例值和隐藏默认值。反射双向核对全部非引擎 Uniform，缺失声明或不存在的声明均拒绝，失败清理本次场景 GPU 资源。
- 材质变更 Shader 或 Mesh 后释放已无人使用的程序或网格缓冲。显式停止项目时，GameplaySession 协调释放场景、脚本资源引用和 GPU 句柄，CPU 资源随最后一个使用者释放。热重载由 GameplaySession 通过 ScriptRuntime 契约停止旧运行，再提交新目录并启动；RenderSystem 在下一次 prepare_scene 根据场景标识释放旧 GPU 资源并建立新资源。GPU 通过 bgfx::copy 持有独立字节，不借用已替换的 CPU 定义；builtin 定义保留至资源系统关闭。编译后的 Shader 必须符合当前渲染后端、顶点和内置引擎 Uniform 契约；不提供 Shader 源码运行时编译。
- Android 暂停关闭 bgfx 时同时释放场景 GPU 资源；恢复后根据仍存活的场景重新建立资源。平台运行验证范围见根 README。
- Android Surface 处理代码集中在平台实现中；APK、模拟器暂停恢复和真机验证范围见根 [README.md](../../../README.md)。

C++ 限定符：构造为 `explicit` 且 `noexcept`，`world` 默认为 `nullptr`；析构、`release_scene()`、只读状态方法和 `on_shutdown()` 为 `noexcept`；只读状态方法为 `const`。析构及受保护钩子标注 `override`，状态返回值标注 `[[nodiscard]]`。

## 几何、纹理与照明

- 几何支持 UV0、切线、顶点色和子网格；GPU 顶点以明确 float 布局组装，不依赖 FlatBuffers struct 的内存布局。网格按所属节点完整 ID 与 mesh_revision 更新，仅改变的几何重新上传。
- 每个子网格按材质槽独立提交，空子网格表绘制完整索引与槽 0。所有材质槽遵循相同 Shader 与命名参数规则。
- 纹理来自 ResourcePtr 持有的 TextureData，Basis Universal 转码后上传全部 mip。auto 按实际 GPU 能力与 Basis 转码器支持情况选择 BC7、ASTC 4x4、ETC2 或 RGBA8；显式请求不支持的格式返回失败。sRGB 只用于颜色纹理，法线及 ORM 使用线性采样。
- GPU 纹理按 ResourcePtr 对象身份共享并只保留当前可绘制材质实际引用，停止项目、场景替换及设备关闭统一释放。Shader sampler 名称、类型及数量与反射双向验证。bgfx 公共反射不提供绑定槽，元数据中的 stage 必须与源码 SAMPLER2D 的绑定一致；运行时另外检查槽唯一及设备上限。纹理包含单层或完整 mip 链，转码工作集上限为 512 MiB。
- 内置 PBR 采样 baseColorTexture、normalTexture、ormTexture（R=AO、G=roughness、B=metallic）；顶点色与 baseColor 相乘。切线空间参考 bgfx 06-bump，PBR 核心直接适配 The Forge ShaderUtilities.h.fsl 的 GGX、Smith 与 Schlick 函数，保留来源和许可。
- 最多 8 个启用灯光，方向、点光、聚光均由节点组件表达；超出限制明确失败，不静默丢灯。相机支持透视和正交投影。
- `set_settings()` 接受调用方明确提交的渲染参数，非法值返回失败且保持原配置。默认环境光为 0、曝光为 1、Reinhard 色调映射、阴影关闭；不改变既有直接照明效果。可选择统一环境漫反射和 filmic 色调映射；环境参数不代表隐藏场景灯，也不实现 IBL。
- 方向光阴影使用场景中第一个启用的方向光，其变换仍来自节点。按当前可绘制网格的世界边界拟合正交投影，复用 bgfx `15-shadowmaps-simple` 的 packed-depth 路径与 16 点 PCF 采样；无方向光不产生阴影。RGBA8 深度编码与 D16 深度附件避免依赖硬件比较采样；纹理、帧缓冲及程序随场景释放，设备不支持附件格式时返回失败，不假报开启。阴影使用保留纹理槽 15，项目材质不得占用该槽。ACES 色调映射直接适配 bgfx `shaderlib.sh` 的曲线，保留来源。
- `request_screenshot()` 仅接受调用方提供的绝对 UTF-8 `.png` 路径，父目录必须存在；核心不选择项目目录、不创建目录。返回 `true` 表示已排队，后续帧经 bgfx 回调写入 PNG 后 `screenshot_status()` 才变为 `complete`；失败为 `failed`，等待期间拒绝第二个请求。回调处理 pitch、BGRA 与 Y 翻转；不借用桌面截图权限。关闭时完成或取消等待中的请求。请求和查询由主线程调用，回调状态跨线程同步。
