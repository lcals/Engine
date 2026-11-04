# 渲染模块

[渲染类图](class-diagrams/render-system.md) · [场景类图](class-diagrams/scene.md) · [分簇/反射/烘焙扩展契约](class-diagrams/forward-lighting-reflections.md)

[bgfx](https://github.com/bkaradzic/bgfx) 提供跨平台图形接口。macOS / iOS 固定使用 Metal，Windows 使用 DirectX 12，Android / Linux 使用 Vulkan。所选后端不可用时初始化失败，不以 Noop 或其他后端回退代替成功。

渲染阶段、目标缓冲、GPU 资源生命周期和绘制提交的代码组织优先适配上游范例，再接入引擎的节点、组件与资源接口。项目可自定义 Shader、具名参数和材质；框架复用不要求保留范例的固定外观或参数。

## 实现职责

公开参数位于 `include/engine/runtime/render/render_settings.hpp`，只依赖标准库。除平台表面实现外，下表中的私有代码均位于 `src/engine/runtime/render/`：

| 文件或目录 | 职责 |
| --- | --- |
| `render_system.cpp` | 系统生命周期、帧提交、统计开关及调用方入口 |
| `render_scene.cpp`、`render_scene_resources.cpp`、`render_settings.cpp` | 场景绘制与资源准备调度、组件参数读写及失败回滚 |
| `scene_gpu_resources.*`、`resources/` | 场景 GPU 集合和逐类型资源的拥有、上传、绑定与释放 |
| `passes/`、`shaders/` | 共用的阴影、AO、HDR/Bloom 通道与 Shader；IBL 采样沿用共用材质 Shader |
| `render_surface.*`、`platforms/<platform>/render/` | 表面状态契约、固定后端选择、原生句柄与暂停恢复 |
| `renderer_callbacks.*` | bgfx 日志回调、截图写入及线程间回执 |

CSM、AO、HDR、Bloom 和 IBL 在各平台使用同一套通道与 Shader 源码，经 `shaderc` 编译到对应后端。平台实现负责后端与表面适配，效果中的必要能力检查依据实际访问契约处理，例如 AO 在 Metal 上的纹理访问判据。能力不足时明确返回失败，不静默丢弃已开启的效果。共用实现不代表已经完成全平台实测；具体执行范围见[验证记录](../testing.md)。

## 窗口与场景

编辑器复用同一个 SDL3 窗口。待机时清屏；启动项目后读取世界系统的活动场景，通过活动 Camera 绘制节点上的 MeshRenderer；停止后释放场景和项目 GPU 资源，恢复空白帧。窗口和共享图形设备保留到宿主关闭。

可见物体、相机和光源全部以 `Node + Component` 表达。渲染器不创建隐含的相机、灯光或物体，也不持有跨帧组件指针。一个场景选择一个活动相机，累加预算内的启用方向光、点光或聚光（默认 256，可配置至 4096）；超出数量返回失败。场景绘制要求有效的观察相机，可绘制来源包括启用的 MeshRenderer、3D 文字及渲染扩展；UI 独立绘制，不能以没有网格判断整个窗口为空白。没有 Light 时 PBR 不获得直接照明，无光照材质仍可显示。

灯的颜色及 `intensity` 在线性空间参与计算，目前强度是引擎相对量，尚未标定为 lux 或 candela。方向光没有距离衰减；点光和聚光以 `max(d², 0.01)` 作距离平方分母，并乘范围淡出 `clamp(1 - d/range, 0, 1)²`；聚光另外在内外锥夹角余弦之间执行平滑过渡。该范围淡出是有界光源的实时近似；设置数值不能直接当作其他 DCC 中同名物理单位的等价数值。

调用方可向 `prepare_scene(camera)` 和 `render(camera)` 显式提交借用镜头，替代本次绘制使用的活动相机。该镜头可以属于调用方独立场景，渲染器不持有它、不改写项目场景数据；未传入时继续使用项目活动镜头。编辑器预览使用独立的隐藏镜头，后处理仍读取项目场景活动相机的 `PostProcessComponent`，不将预览视角保存到场景。

节点世界矩阵为列主序，局部 -Z 表示相机朝向和方向光传播方向。相机提供透视视场角或正交竖直半高及近远裁剪面，窗口像素尺寸决定宽高比；视图基忽略缩放大小，避免非均匀缩放扭曲镜头。模型法线使用世界矩阵逆转置；负缩放按行列式切换剔除方向。窗口缩放更新交换链和投影。

## 网格、材质与 Shader

网格由独立资源提供位置、法线、UV0、烘焙 UV2、切线、顶点色和索引；子网格索引范围关联材质槽。`MeshRenderer.GetMaterial(slot)` 访问 0..15 的材质槽，`SetMaterialCount(count)` 设置槽数。动态网格通过 C++ `set_mesh_data()` 提交生成模型的深拷贝，成功后递增修订号并更新 GPU；未落盘的动态网格不能保存成包含无效 GUID 的场景。示例 `.mesh` 中的方块有 24 个顶点、36 个索引，每面使用独立顶点；运行时从导入二进制建立 GPU 缓冲。材质和 Shader 通过目录中的唯一逻辑路径定位并按需加载，项目路径直接相对资源根，内置路径以 `builtin://` 标识来源。项目材质可复用内置 Shader，内置定义不引用项目资源。

内置 Shader 使用宿主 `shaderc` 在构建时编译 bootstrap 数组，供无项目启动与编辑器基础定义使用；运行包另由宿主 `shaderc` 按目标平台生成内置 Shader 实体，随 `assets/packs/builtin-*` 部署，有外置目录条目时必须读取并校验该实体。项目 `.shader` 描述引用独立源码 GUID，由导入器编译为目标平台二进制；runtime feature 也声明宿主 `bgfx[tools]` 依赖。目标后端固定为 Metal、DirectX 12 或 Vulkan，不依赖运行目录中的示例 Shader 文件。可用 `ENGINE_SHADERC_EXECUTABLE` 指定宿主工具，缺失或无法执行时配置失败。

骨骼动画通过 `MeshRenderer::set_skinning_palette` 提交矩阵，由渲染 compute 生成实例顶点流；主画面、AO 与阴影共用输出，包围盒按骨骼影响更新，详见[GPU 蒙皮与共享几何](#gpu-蒙皮与共享几何)。渲染器不执行骨架采样或读取 FBX。

调用方仍可通过 `MeshRenderer::set_deformation` 直接覆写位置、法线及切线，与 palette 路径互斥。此路径参考 bgfx `35-dynamic` 建立独立动态 VB，变形修订变化时只更新顶点，不重建静态索引缓冲；清除变形恢复源 VB 并释放实例输出。两条路径均保留源网格、材质槽、UV、颜色、索引及子网格范围，全部场景通道读取相同的实例顶点和更新后边界。

材质实例以名字和数值类型匹配参数，支持 `SetFloat`、`SetV2`、`SetV3`、`SetV4` 与具名 `SetTexture`；未知路径、参数或类型及非法数值返回失败并保留原值。内置材质提供无光照、PBR 和 Blinn–Phong。PBR 采用 Cook–Torrance 模型中的 GGX 分布、相关 Smith 可见性和 Schlick Fresnel，支持基础色、切线空间法线及 ORM（AO、粗糙度、金属度）贴图，颜色纹理由 sRGB 解码至线性空间。受光照材质叠加直接光、环境光与启用的 IBL；无光照材质不计算这些照明。全部场景材质始终输出线性 RGBA16F，最后统一执行显示转换。HDR 关闭时按不透明颜色 alpha 的权重，只对受光照材质执行曝光、Reinhard 或可选 ACES 映射，保留原有 unlit 外观；开启时全场景统一映射，最终屏幕仍为 SDR，使用标准 sRGB 分段编码。清屏及 3D 字体使用对应的 sRGB 反变换；UI 在最终输出后维持显示空间预乘混合。

默认受光流程统一为 `builtin://materials/default-pbr` → `builtin://shaders/pbr`，采用 metallic/roughness：新 Mesh、新增材质槽、美术模型未显式映射的材质、地形和破坏碎块都使用此入口。Unlit/Blinn–Phong 仅由调用方明确选择，UI、字体及调试绘制按专用用途使用对应 Shader。发光实体使用 PBR 的 `emissionColor`/`emissionTexture`，不通过 Unlit 模拟普通受光表面。

`Material.SetTexture(name, path)` 按 Shader 的 sampler 声明设置贴图，`GetTexture(name)` 返回当前资源路径。内置参数为 `baseColorTexture`、`normalTexture`、`ormTexture`（AO/Roughness/Metallic）；法线与 ORM 使用线性贴图。普通图片的导入压缩、颜色空间与 mipmap 配置见[资源管线](../editor/asset-pipeline.md)。

材质 `.material` 的 `cull` 为 `"back"`（默认）、`"front"` 或 `"none"`，对应生成模型 `CullMode`，脚本使用 `Material.SetCullMode()` / `GetCullMode()`。渲染器按每个子网格材质执行面剔除，并处理负缩放；`none` 用于树叶、布片等双面几何。内置 Shader 在背面可见时将法线朝向观察侧。状态由材质实例持有，编辑器保存材质资源时一起保存，不作为 Shader 的固定业务参数。

内置 PBR/Unlit/Blinn–Phong 提供具名 `alphaCutoff` float 参数（0..1，默认 0 关闭）；覆盖率为基础色纹理 alpha、`baseColor.a` 和顶点 alpha 的乘积。通过 `SetFloat("alphaCutoff", 0.5)` 或 `.material` 的 `[[parameters]]` 设置；低于阈值的片元丢弃，其余按不透明表面写入深度。主 pass 与阴影 pass 共享裁切函数，阴影按子网格取得对应材质的阈值、基础色和纹理；自定义 Shader 须声明同名 `alphaCutoff`（float）、`baseColor`（v4）、`baseColorTexture`（sampler），并在主 pass 执行相同裁切。缺少阈值表示不裁切，启用后缺少配套声明则明确失败。半透明由 Material.blend_mode 单独设置，`.material` 使用 `blend = "alpha_blend"` 或 `"premultiplied"`，按子网格视空间深度排序，不写入不透明深度。

内置 PBR 的 `specularWorkflow = 1` 启用反射色工作流：`specularColor`（v3）乘 `specularTexture.rgb` 得到线性反射色，粗糙度来自 `1 - smoothness * specularTexture.a`；默认 0 仍走原有 metallic/roughness 与 ORM。SpecGloss 纹理的颜色通道按资源 sRGB 配置解码，alpha 保持线性。`normalScale`（0..8，默认 1）缩放切线空间法线的 XY。`uvTransform`（v4，默认 1,1,0,0）给所有表面纹理提供 scale.xy/offset.zw，阴影裁切使用同一变换；自定义 Shader 缺少该参数时阴影使用原始 UV。

直接光以插值的基础法线判断光源是否处于当前表面一侧，避免法线贴图将背面的光带到正面；双面材质按朝向后的贴图法线选择表面一侧。PBR 的直接漫反射进一步采用 [Ray Tracing Gems 第 12 章的微表面遮蔽](../../third_party/notices/ray-tracing-gems-terminator.md)，平滑扰动法线在基础表面明暗交界处的响应；同样用于漫反射烘焙捕获。系数保持固定上游值，没有按截图拟合；镜面 GGX 和预卷积 IBL 不额外乘该因子。这是使用平滑基础法线的近似，不是三角面几何法线或真实位移，也不替代场景阴影；glTF 没有强制规定唯一的终止线校正方法。

ORM 的 R 通道表示材质环境遮蔽，衰减环境光和环境反射，不衰减直接光与自发光；屏幕空间 AO 也是间接照明的遮蔽。法线缩放按切线空间法线 XY 缩放后重新归一化，遵循 [glTF 2.0 材质规范](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#materials)；不同 DCC 的法线强度控件不一定使用同一公式。正交相机使用平行视线，透视相机使用片元到相机的视线。

两种 PBR 工作流的直接光均参考 glTF 2.0 附录 B，采用 Schlick Fresnel（F90 = 1）和相关 Smith GGX 可见性；金属粗糙度工作流将介质与金属的完整响应按金属度混合。反射色工作流按 [KHR_materials_pbrSpecularGlossiness](https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Archived/KHR_materials_pbrSpecularGlossiness/README.md) 定义 `F0 = specular`、`c_diff = diffuse * (1 - max(specular))`，再按 glTF BRDF 使用 `(1 - F) * c_diff / π`。该扩展已被 Khronos 归档，材质仍沿用项目自身的纹理及光滑度参数约定。GGX 分布采用数值稳定的等价形式，避免低粗糙度高光被固定分母阈值削平。当前最小粗糙度为 0.045；glTF 附录 B 同时指出其 Fresnel 混合仍是近似，不应把它视为完美的物理能量守恒。

启用 IBL 时，金属粗糙度材质参照固定版本 [Khronos glTF Sample Renderer](../../third_party/notices/gltf-sample-renderer.md) 分别计算介质与金属的环境 Fresnel 和多次散射后混合，介质漫反射按其反射比例衰减；反射色工作流对其 `c_diff` 使用相同的环境 Fresnel 混合。SSR 仅替换可由现有表面缓冲中 F0 重建的线性 split-sum 项，其余环境反射保留，因此 SSR 仍是近似。IBL 未启用或没有有效 LUT 时，环境光、光照贴图及探针的漫反射使用 Three.js DFG 拟合估计 Fresnel 能量分配；SSR 使用同一 DFG 回退。关闭 IBL 后，即使已有烘焙纹理仍被加载，SSR 也不继续使用其 LUT。合成白炉 GPU 测试和平台限制见 [测试文档](../testing.md)。

`builtin://shaders/blinn-phong` 提供 Lambert 漫反射与 Blinn–Phong 高光，默认材质为 `builtin://materials/default-blinn-phong`；仍使用基础色、法线、SpecGloss 与发光的命名参数。直接光高光颜色为 `specularColor * specularTexture.rgb`，指数为 `2^(10*smoothness+1)`，不执行 PBR 的能量扣减与漫反射除 π。直接光响应适配 bgfx `18-ibl/fs_ibl_mesh.sc` 的 Lambert、半程向量幂函数与光滑度指数映射；为经验型材质移除其高光归一化、额外 N·L 与 Fresnel，并将指数偏置设为 1。环境照明复用本引擎的 GGX split-sum IBL 路径，不等同于该范例的完整材质模型。

PBR 反射色流程与 Blinn–Phong 的 `smoothnessSource` 默认 0，光滑度为 `smoothness * specularTexture.a`；设为 1 时直接使用 `baseColorTexture.a * baseColor.a`，不再次乘 smoothness 或顶点 alpha。两个有光照 Shader 的 `specularHighlights` 默认 1，设为 0 关闭直接高光。参数仍由 Shader 资源声明与验证，核心 Material 没有专用字段或算法枚举。

`emissionColor`（v3，0..10000，默认 0）乘 `emissionTexture.rgb` 在线性照明中叠加发光，之后参加雾与色调映射；它不创建 Light，也不产生实时几何间接光。标记为烘焙贡献者的静态表面可通过离线光照烘焙照亮周围对象。HDR 与 Bloom 默认关闭，显式开启后，高亮发光与其他高亮场景颜色共同按线性阈值参与泛光。具名 `fogInfluence`（0..1，默认 1）控制全局雾的影响，天空表面可设 0。

Shader `.shader` 的 `[[samplers]]` 声明 `name/uniform/stage/default_texture/exposed`；默认纹理只允许内置资源。普通材质资源 `stage` 为 0..4，必须与 Shader 源码 `SAMPLER2D` 槽号一致；完整系统槽位见[分簇光照、反射、水面与静态间接光](#分簇光照反射水面与静态间接光)。材质与 UI 的普通纹理引用只接受二维纹理，IBL cube 由环境组件单独绑定。自定义 Shader 须实现相应采样，声明材质参数不会自动获得系统效果；bgfx 公共反射能校验名称、类型与数量，不能反射槽号。`.material` 的 `[[textures]]` 使用 `name` 与 `texture` GUID 指定覆盖值，资源默认纹理与材质纹理都进入统一依赖树。动态帧纹理由单独的 `source` 契约声明和绑定，不作为项目纹理资源引用。

编辑器 MCP `set_camera`、`set_material_texture`、`set_light` 操作相机、材质与灯；`set_light` 支持 `kind/range/innerAngle/outerAngle`，材质命令支持可选 `slot`。场景查询返回全部材质槽和贴图，修改沿用批量事务、撤销/重做与完成回执。历史记录资源身份与版本，不让停止后的资源因撤销栈继续常驻。

缓冲提交参考固定版本 bgfx `01-cubes`，切线与法线采样参考 `06-bump`；PBR 函数适配 The Forge `Common_3/Graphics/ShaderUtilities.h.fsl`。方向光阴影的 packed-depth 与 PCF 适配 bgfx `15-shadowmaps-simple`，级联划分与稳定投影参考 `16-shadowmaps`。alpha 阈值 discard 与指数雾分别适配 `16-shadowmaps/fs_shadowmaps_color_texture.sc`、`fs_shadowmaps_color_lighting_main.sh`：裁切改为可配置 alpha，雾改为前向深度、线性颜色与可配置模式，并用负指数避免溢出。可选 ACES 曲线来自 bgfx `shaderlib.sh`。源码保留上游来源，许可位于 `third_party/notices/`。

内置 Shader 由 `cmake/core/render_shaders.cmake` 调用宿主 shaderc 编译并嵌入目标；项目 `.shader` 引用独立源码 GUID，经资源管线生成目标平台二进制。Metal 使用 Metal 程序，DirectX 使用 Shader Model 5，Vulkan 使用 SPIR-V。宿主工具不存在或不可执行时配置失败；`ENGINE_SHADERC_EXECUTABLE` 可显式指定工具。输入及工具未变时复用生成物。

## 渲染设置与真实帧截图

`RenderCommands` 在编辑器主线程编辑组件，并调用渲染器查询快照、提交设备设置与截图；模块函数 `register_render_tools` 只登记并转发以下工具：

| 工具 | 参数与作用 |
| --- | --- |
| `get_render_settings` | 无参数；读取当前场景组件组成的渲染快照 |
| `set_render_settings` | 环境光、雾、IBL、AO、`hdrEnabled`、`bloomEnabled`、`bloomThreshold`、`bloomIntensity`、`bloomRadius`、`exposure`、`filmicTonemap`、方向光阴影与级联、`clearColor` 等至少一项；活动场景中修改相应组件并支持撤销、保存 |
| `capture_frame` | `path`，相对于项目 `cache/` 的 `.png` 路径；请求真实引擎帧截图 |
| `get_frame_capture` | 无参数；查询截图 `state` 与文件 `path`，`complete` 才表示文件写入完成 |
| `set_render_stats` | 必填 `enabled` 布尔值；开关 bgfx 自带统计面板，默认关闭 |

`set_render_stats {"enabled":true}` 开启面板，`{"enabled":false}` 关闭。开关在空白待机、场景预览和玩法运行中均生效，切换场景或模式保留当前状态；不进入撤销、不标记场景修改，也不随场景保存。面板直接使用 `BGFX_DEBUG_STATS`，不额外启用逐 View Profiler。

Tracy、CPU/GPU 统计及采集工具统一见[性能采样](performance.md)；渲染统计面板与 Tracy 开关相互独立。

截图由 bgfx 帧回调输出 PNG，场景必须继续提交帧。编辑器校验相对路径并创建父目录，渲染器仅接受宿主提供的明确路径；MCP 不自行绘制图片。命令回执完成只表示请求已接受，截图自身的 `pending` 还须通过 `get_frame_capture` 查询；命令通道的 `activation: pending` 则按原请求身份查询 `get_editor_receipt`，不重复提交。

原生调用方可用 `RenderSystem::request_hdr_capture()` 排队捕获下一帧正式场景，并继续调用 `render()`，通过 `hdr_capture_status()` 查询；状态为 `complete` 后使用 `take_hdr_capture()` 接收 `SceneCaptureImage`。RGB 是线性浮点数，允许大于 1；返回尺寸是实际场景目标的物理像素，行从上至下、RGBA 紧密排列。源数据为 RGBA16F，扩展为 float 返回，Alpha 原样保留。结果包含材质、光照、反射、透明和场景扩展，位于 Bloom、曝光、色调映射及 UI 之前；它不是显示器 HDR 输出或 PNG 截图。

同一时刻只接受一个未取走请求，未初始化、没有活动场景或上次读回仍在退役时请求失败。提交帧时才检查可绘制内容及实际相机，因此可以在请求后通过 `render(camera)` 指定独立场景中的相机；若提交帧没有有效相机或可绘制内容，请求会取消。`cancel_hdr_capture()`、场景释放、设备重建及目标尺寸改变会取消当前结果；即使已取消，bgfx 仍须完成在途读回后才能回收缓冲。平台不支持 blit、读回或所需格式时状态为 `failed`，不会退回 SDR。MSAA 读取透明合成目标 resolve 后的结果。原始浮点值的实际用途和图像文件编码由调用方决定。

`request_screenshot()` 接收调用方提供的绝对 UTF-8 PNG 路径，仅负责排队。宿主继续提交帧，bgfx 回调通过已选 bimg 编码器和 SDL3 IO 写出原始渲染结果，`screenshot_status()` 报告 pending、complete 或 failed。回调正确处理 BGRA、行步长及原点翻转；写入失败不会报告完成。路径选择、目录创建和编辑器相对路径权限检查由调用方负责，核心无需桌面截图权限。

## 场景环境参数

场景环境光、IBL、雾与清屏色由普通节点的 `EnvironmentComponent` 持有；曝光、色调映射、屏幕空间 AO、HDR 与 Bloom 由活动相机同节点的 `PostProcessComponent` 持有；方向光阴影参数由 `Light` 持有。一个场景最多允许一个启用环境和一个启用投影的方向光，重复时明确失败。渲染每帧读取组件，不跨帧持有组件指针；组件须同时满足 `active()` 与 `visible_in_hierarchy()` 才参与渲染；缺少、禁用或隐藏组件时使用固定默认值，切场景不继承前一个场景的外观。

`RenderSettings` 是这些组件组成的只读帧快照，`RenderSystem::settings()` 只负责读取；环境、后处理和灯光组件分别承担写入与校验。无场景时 `set_idle_clear_color()` 只修改待机清屏色，不影响以后加载的场景；编辑器 `set_render_settings` 此时也仅接受 `clearColor`。默认环境光强度为 0，IBL、AO、HDR、Bloom 与阴影关闭，曝光为 1，使用 Reinhard。核心不识别调用方是编辑器、项目还是截图工具。

可选三色环境光按世界法线 Y 在天空、赤道与地面颜色间插值，再乘环境强度。常量/三色环境光与 IBL 独立，可同时贡献照明；调试曝光时需一并检查二者，避免重复补光使画面发灰。`fog_mode` 可选 none、linear、exponential、exponential_squared。线性雾在透视相机下使用片元到相机的世界距离，在正交相机下使用前向深度，从 `fog_start` 到 `fog_end` 混合线性雾色；指数雾与平方指数雾使用相机前向深度减近裁面后的非负距离 d，透过率分别为 `exp(-fog_density*d)` 与 `exp(-(fog_density*d)^2)`，随后进行输出处理。fog_density 为非负有限值，0 表示没有衰减。三色环境和雾默认关闭，颜色参数为线性 0..1；这些值属于环境组件，不含具体资源来源的特殊分支。

脚本 `EnvironmentComponent.SetAmbientTricolor(sky, equator, ground, intensity)` 接受三个线性 `[r,g,b]` 数组；`SetAmbient()` 切回单色环境光。`EnvironmentComponent.SetLinearFog(enabled, r, g, b, start, end)` 要求 `0 <= start < end`；`EnvironmentComponent.SetExponentialFog(enabled, r, g, b, density, squared=false)` 在 `squared=true` 时使用平方指数雾。MCP `set_render_settings` 对应字段为 `ambientTricolor`、`ambientSkyColor`、`ambientEquatorColor`、`ambientGroundColor`、`fogMode`、`fogColor`、`fogStart`、`fogEnd`、`fogDensity`，并保留 `linearFog` 兼容入口。

项目初值保存到 `.scene`。脚本通过 `Node.GetComponent` / `AddComponent` 访问[渲染组件接口](../plugins/runtime/class-diagrams/render-script-api.md)，并检查各方法返回值；脚本修改不自动写入磁盘。`Render` 静态门面只保留视口尺寸与 VSync。每个组件独立校验，一次无效提交保留该组件原有设置。编辑器 MCP `set_render_settings` 可按需创建环境节点和相机后处理组件，修改进入撤销历史及未保存状态，再由 `save_scene` 落盘。曝光、AO、HDR 与 Bloom 修改要求已有活动相机，开启阴影要求已有方向光。脚本清屏色为 `0xRRGGBBAA` 无符号整数，MCP 对应参数为归一化 `[r,g,b,a]` 数组，通道顺序相同。

```toml
[[nodes]]
id = "10"
name = "Environment"
parent = "0"

[[nodes.components]]
type = "environment"
ambient_color = [0.82, 0.87, 1.0]
ambient_intensity = 0.28
ibl_enabled = false
ibl_intensity = 1.0
fog_mode = "exponential_squared"
fog_color = [0.34, 0.43, 0.58]
fog_density = 0.007
clear_color = 0x7496b0ff
```

相机节点可追加 `type = "post_process"`、`exposure = 1.05`、`filmic_tonemap = true` 的组件；方向光组件可设置 `cast_shadows = true`、`shadow_resolution = 4096`、`shadow_bias = 0.0015`、`shadow_cascades = 4`、`shadow_distance = 150`、`shadow_split_lambda = 0.6`、`shadow_cascade_blend = 0.1`。详细接口见[环境与后处理类图](class-diagrams/environment-components.md)及[渲染类图](class-diagrams/render-system.md)。

`.scene` 的组件类型分别为 `environment`、`post_process`、`light`，由同一份 FlatBuffers 生成数据承载导入、保存、重载与运行。天空使用普通网格和材质。

`Render.Set*` 修改已有组件，缺少对应组件返回 false。推荐先加载场景，再通过 `Node.GetComponent` 修改；需要新组件时由项目显式 `AddComponent`。场景初值应写入 `.scene`，不能在 `Scene.Load` 前设置并期待跨场景继承。聚合阴影接口优先操作当前投影的方向光，否则选择首个启用方向光；关闭投影后查询可能显示首灯的参数，逐灯修改使用 `Light`。

## 阴影与级联

MeshRenderer 的 `cast_shadows` 默认 true；设为 false 后，该组件仍在主 pass 绘制，但完全排除于阴影边界拟合和投射，避免天空模型拉大阴影范围。场景 TOML 同名字段及脚本 `SetCastShadows()` 操作同一生成状态。

方向光阴影读取唯一启用 `cast_shadows` 的 `Light`，支持 1、2、4 级 CSM；默认 4 级、阴影距离 150、对数/均匀划分混合比例 0.6、级联交界混合比例 0.1。对应场景字段为 `shadow_cascades`、`shadow_distance`、`shadow_split_lambda`、`shadow_cascade_blend`。距离范围为 `(0, 100000]`，两项比例范围分别为 `[0, 1]`、`[0, 0.3]`；实际远端取相机远裁面与阴影距离的较小值，远端不超过近裁面时不提交阴影。显式传入的观察镜头决定级联范围，预览和玩法共用同一实现。逐灯配置归组件，渲染器不创建场景节点或保存编辑器策略。

每级覆盖镜头的一段视锥，使用包围球固定正交范围并将中心对齐纹素；深度范围纳入与本级范围相交的投射物。方向光区域与局部光面共用 atlas，聚光单面、点光六面由预算分配。`shadow_resolution` 表示方向光级联区域边长，完整 atlas 大小由 RenderConfig 控制。当前 view 分区与灯/阴影绑定见[扩展类图](class-diagrams/forward-lighting-reflections.md)。

主 pass 按观察镜头的正向深度选级，16 点 PCF 的每次采样限制在本级 tile 内。每级末尾的混合区与下一层平滑过渡，最后一级在同样区间淡出；混合比例为 0 时使用硬切换。下一级投影向前扩展以覆盖混合区。坐标变换处理各后端的屏幕原点和深度范围；超出有效投影或阴影距离返回全可见。没有投影方向光时仍可为点光与聚光提交 atlas；没有任何阴影面时释放专属 GPU 对象。聚合设置只查询和修改方向光，逐灯编辑应直接使用 `Light`。

CSM 参考引入时固定的 [bgfx `16-shadowmaps`](https://github.com/bkaradzic/bgfx/tree/7a70927d2af7ffcc5cef9c9e8f4c821e31fcb099/examples/16-shadowmaps)（commit `7a70927d2af7ffcc5cef9c9e8f4c821e31fcb099`）：`shadowmaps.cpp` 提供混合划分和纹素稳定思路，`fs_shadowmaps_color_lighting_main.sh` 提供分级采样结构。引擎适配为单 atlas、相机深度选级、包围球稳定范围、交界重叠混合和末级淡出，PCF 保留 `15-shadowmaps-simple/fs_sms_shadow.sh` 的 4×4 布局，并增加 tile 边界限制。运行库升级不改写原始算法来源；Dario Manesku 的示例版权与 BSD-2-Clause 条款保留在 [bgfx 许可](../../third_party/notices/bgfx.txt)。

`shadow_bias` 是共用的归一化深度偏移。级联距离、投射物深度范围和场景尺度改变后，应重新对比斜面自阴影与接触阴影；偏移过小可能出现条纹，过大可能使投影脱离物体。纹素对齐和交界混合不代替偏移调整，也不保证任意运动下完全无闪烁。

例如 `set_render_settings {"directionalShadows":true,"shadowResolution":4096,"shadowCascades":4,"shadowDistance":150,"shadowSplitLambda":0.6,"shadowCascadeBlend":0.1}`。数量和距离等配置可独立修改，保持阴影启用状态。脚本对指定方向灯调用 `Light.SetShadowCascades(count, distance, splitLambda?, blend?)`；`Light.SetShadows` 保留已有级联配置。

`shadowResolution` 是方向光级联区域边长；完整 atlas 预算来自项目 `settings/parallel.toml` 的 `[defaults.render].shadow_atlas_size`，平台档可覆盖。点光六面和聚光单面通过原有 `Light.SetShadows` 申请同一 atlas；只有实际分配成功的灯启用阴影，空间或面数不足会报告预算诊断。非不透明材质不投实体阴影。

## 分簇光照、反射、水面与静态间接光

实现沿用 RenderSystem、普通组件、材质资源与 scene_bake 插件；来源、固定版本和限制见[上游适配记录](rendering-upstream-plan.md)。

- 灯光使用全视锥深度分簇，透明面按自身深度取灯；局部簇超预算时遍历完整灯列表，保持照明正确。项目 `settings/parallel.toml` 的 `[defaults.render]` 配置预算，移动端覆盖使用 `[profiles.mobile.render]`，运行包读取汇总设置中的 `parallel.render`。
- 相机 `PostProcessComponent.SetScreenSpaceReflections(enabled, resolutionScale?, maxSteps?, maxDistance?, thickness?, roughnessCutoff?)` 开启 SSR；材质法线、粗糙度与 F0 来自共享表面 Shader。SSR 命中时替换 IBL 镜面的线性 split-sum 子项，其余环境反射近似保留；未命中保留原环境反射。没有启用 IBL 时，SSR 使用 DFG 拟合计算屏幕反射，不依赖残留的烘焙 LUT。首版没有时序降噪，屏幕外对象使用平面捕获或 IBL。
- 反射面所在普通 Node 添加 `PlanarReflectionComponent`，局部 XZ 为平面；`SetSettings(resolutionScale, clipBias)` 设置捕获。独立镜像相机绘制建筑、裁切材质和蒙皮角色，不递归捕获透明面。超预算的面使用环境回退。
- `builtin://shaders/water` 和 `builtin://materials/default-water` 提供双层流动法线、Fresnel、实时平面倒影、深度约束的屏幕折射及吸收。水仍是普通材质，参数由 `SetFloat/SetV*` 设置，常用参数为 `waterIor`（默认 1.333）、`waterFlow`、`refractionStrength`（0..1，默认 1，按折射率投影到背景）、`absorptionDistance`、`absorptionColor`、`shoreFadeDistance`、`reflectionStrength`。冻结的不透明颜色和透明输出、采样深度与深度附件分别保存，避免读写同一纹理。
- Lightmap 按 MeshRenderer 实例绑定，UV2 使用 xatlas；在接收网格世界包围盒中心采样 SH9 探针。只有显式 `bake_static=true` 的不可变网格参与静态烘焙；角色、碎块和可破坏对象默认只接收实时直射与探针。网格、实例或探针体积变换改变后旧结果失效。

照明烘焙前保存场景并保持预览，标记静态接收/遮挡网格后调用 `bake_scene` 的 `effects:["lighting"]`。`lightNodeIds` 显式指定参与烘焙的灯，空列表只采天空与自发光；不自动把运行时灯烙入。`probeVolumes` 显式指定体积节点及局部边界/网格。多反弹从前一轮 irradiance 读取，经真实场景漫反射捕获生成下一轮，不重复累加直射光。作业参数、源资源发布与取消边界见[场景烘焙](../plugins/editor/scene-bake.md#场景-lightmap-与光照探针烘焙)及[烘焙类图](../plugins/editor/class-diagrams/scene-lighting-bake.md)。

局限：SSR 没有离屏信息与时序降噪；透明采用子网格排序，不解决相交面的逐像素排序、多层折射。HDR 关闭时，混合像素通过单通道权重近似区分受光照与无光照贡献；加色和多层混合不能精确分离各层曝光。静态 GI 不随破坏实时重算，探针单点插值不能自动消除跨薄墙漏光。实际平台范围见[验证记录](../testing.md)。

材质资源贴图使用槽 0–4；5/7 为灯和簇 buffer，6 为实例 Lightmap，8–10 为平面反射/冻结场景颜色/采样深度，11 为 AO，12–14 为 IBL，15 为阴影。场景纹理由 `.shader` 的 `source` 声明选择，由引擎绑定固定槽位；不是项目纹理资源，不生成 GUID 依赖。完整通道顺序与绑定契约见[扩展类图](class-diagrams/forward-lighting-reflections.md)。

## 屏幕空间环境遮蔽

AO 适配固定 bgfx commit `7a70927d2af7ffcc5cef9c9e8f4c821e31fcb099` 的 `39-assao`，使用固定 Q2 与两次边缘保持模糊。`SurfacePass` 先绘制全部不透明遮挡深度，再用可选表面程序填真实材质数据；AO 借用独立 D32F，内部中间图补齐工作组，最终输出保持实际视口尺寸。关闭 AO 不改变 SSR/水面仍使用的表面深度。

`PostProcessData` 保存 `ao_enabled`（默认 false）、`ao_radius`（默认 1，范围 `(0,100]`）、`ao_strength`（默认 1，范围 `[0,2]`）、`ao_power`（默认 1.5，范围 `[0.1,8]`）和 `ao_fade_start` / `ao_fade_end`（默认 80/150，要求 `0 <= start < end <= 100000`）。半径与淡出距离使用世界单位，参数必须有限；关闭开关保留其他设置。参数由活动场景相机提供，预览隐藏镜头只决定本帧投影与深度，支持透视和正交相机。MCP 的同名驼峰字段和脚本 `SetAmbientOcclusion` 复用组件校验与持久数据；MCP 编辑进入撤销链，再由 `save_scene` 保存，脚本修改不自动写盘。不维护单独的编辑器效果状态。

MCP 开启示例：`set_render_settings {"aoEnabled":true,"aoRadius":1,"aoStrength":1,"aoPower":1.5,"aoFadeStart":80,"aoFadeEnd":150}`；关闭只需 `{"aoEnabled":false}`。 脚本可用 `PostProcessComponent.SetAmbientOcclusion(enabled, radius?, strength?, power?, fadeStart?, fadeEnd?)`，保留曝光及其他后处理设置；非法参数整次失败。

AO 只衰减内置受光照材质的环境光与 IBL；直射光、自发光、无光照材质、天空和 UI 不受屏幕空间 AO 压暗。项目 Shader 可按 `s_ambient_occlusion` 与 `u_ambient_occlusion` 契约采样，保留纹理槽 11；IBL 使用 12..14、阴影使用 15，项目资源 sampler 限于 0..4，导入、运行时资源校验和反射遵守同一规则。

开启时按窗口尺寸准备深度、法线及 AO 中间纹理，关闭、场景释放、设备重建或失败时清理。结果每帧重新计算，只存在于 GPU，不生成贴图源文件、meta 或 GUID 资源，也不依赖离线烘焙；场景只保存 AO 参数。所需计算或纹理能力不可用时明确返回失败。它使用当前可见深度，不替代场景几何烘焙或屏幕外遮挡信息。

Metal 能力检查沿用上游实际访问契约：本通道的 R16F、RGBA8、RG8、R8 仅使用分离的 `read` / `write` 绑定，不要求单个 Shader 的 `read_write` 访问。bgfx Metal 的 `IMAGE_READ/WRITE` 标志来自后者的 tier，不能据此拒绝可用的 RG8；仍检查 compute、纹理数组、绑定数量及 D32F 附件能力。其他后端保留独立 image 能力检查。

## HDR 场景合成与 Bloom

线性场景颜色的 alpha 是不透明色调映射权重（PBR/Blinn 为 1、unlit/背景为 0），SSR 原样保留。受光照透明表面按覆盖度加入权重；透明 unlit 和 3D 字体只按覆盖度衰减已有权重，不添加自身权重。HDR 开启时统一色调映射，关闭时保留 unlit 与字体的原有色彩；最终 gamma 转换只有一次。这个单通道权重是混合像素的近似：lit 与 unlit 的半透明叠加或加色特效不能分别在映射前后精确合成，本轮不引入额外分层颜色目标。

HDR 与 Bloom 适配固定 bgfx commit `7a70927d2af7ffcc5cef9c9e8f4c821e31fcb099` 的 [09-hdr](https://github.com/bkaradzic/bgfx/tree/7a70927d2af7ffcc5cef9c9e8f4c821e31fcb099/examples/09-hdr)，使用其全屏绘制和多通道组织、九次采样亮部提取、纵向 `blur9`、横向 `blur9` 与合成流程。保留 `blur9` 权重 `1 / 0.9 / 0.55 / 0.18 / 0.1` 和 `4.46` 归一化；适配为中心对称 3×3 提取核、线性 RGBA16F 中间值、一次最终输出转换及引擎资源生命周期。源码、调整说明和 BSD-2-Clause 许可见 [HDR 来源](../../third_party/notices/bgfx-hdr.md)。

`HdrPostProcessPass` 是渲染私有实现。场景始终以线性 RGBA16F 中间目标完成反射和透明合成：不透明主程序同时保存 IBL 镜面贡献，SSR 输出独立颜色；透明读取冻结场景颜色并写另一个颜色目标，保留不透明深度测试且不写深度。特效与 3D 字体合入之后，Bloom 可选执行亮部提取与双轴模糊，最终统一曝光、Reinhard/ACES 和 gamma 编码，UI 随后绘制。

HDR 指场景内部保留高于 1 的线性亮度，最终交换链和截图仍输出 SDR，没有 HDR 显示器色域或输出编码。曝光由已有参数显式控制，未引入上游的亮度测量、平均亮度贴图或自动曝光；Bloom 阈值在曝光前应用。清屏 RGBA8 的 RGB 先按原显示编码解码为线性值，再与场景共同进入最终输出。UI 在 View 200 绘制，不进入场景 HDR 目标，不受场景曝光、色调映射或 Bloom 影响。

项目表面 Shader 必须遵守 `u_tonemap`：`x` 为曝光、`y` 为 ACES 开关、`z > 0.5` 表示本帧要求原始线性颜色。此时 Shader 只输出线性场景结果，不执行曝光、色调映射或 gamma；内置 Shader 将 RGB 限制在 RGBA16F 有限范围 `[0,65504]`。自定义字体 Shader 的 `u_font_texture.xy` 仍为曲线纹理尺寸，`z > 0.5` 表示线性 HDR 输出，需保留覆盖率与预乘 alpha 语义；内置实现先解码未预乘颜色，再重新预乘。UI 字体的该标志为 0。Shader 若忽略此契约会产生重复转换，声明普通材质参数不会自动修复。

参数由活动场景相机的 `PostProcessData` 保存，预览隐藏镜头只提供观察视角，沿用同一份参数：

| 场景字段 | MCP 字段 | 默认值 | 有效范围 |
| --- | --- | --- | --- |
| `hdr_enabled` | `hdrEnabled` | `false` | bool |
| `bloom_enabled` | `bloomEnabled` | `false` | bool；开启要求 HDR 同时开启 |
| `bloom_threshold` | `bloomThreshold` | `1` | 有限值 `[0,100]`，曝光前线性颜色阈值 |
| `bloom_intensity` | `bloomIntensity` | `0.15` | 有限值 `[0,10]`，合成强度 |
| `bloom_radius` | `bloomRadius` | `1` | 有限值 `[0.25,4]`，模糊采样间距倍率 |

Bloom 与 HDR 的依赖严格校验，不自动开启或关闭另一项。MCP 可在一次 `set_render_settings` 中提交 `{"hdrEnabled":true,"bloomEnabled":true,"bloomThreshold":1,"bloomIntensity":0.15,"bloomRadius":1}` 或 `{"bloomEnabled":false,"hdrEnabled":false}`，由编辑器组织各组件提交与失败回滚，支持撤销、重做与 `save_scene`。非法组合、类型、范围或非有限数值整次失败，保持原设置。

脚本通过相机上的 `PostProcessComponent.SetHdr(enabled)` 与 `SetBloom(enabled, threshold = 1, intensity = 0.15, radius = 1)` 修改后处理。先开 HDR 再开 Bloom，先关 Bloom 再关 HDR；每次检查 bool 返回值，顺序错误返回 false 并保留全部设置。旧曝光和 AO 接口保留 HDR/Bloom 字段，关闭 HDR/Bloom 不改变其他后处理参数。脚本操作不自动保存场景。

窗口尺寸变化时重建中间目标；关闭 Bloom 释放两组 Bloom 目标，场景释放、设备重建或准备失败时释放整套专属 GPU 资源。关闭 HDR 仍保留 Forward+、SSR 与透明水共用的线性场景目标，只改变最终映射策略。设备必须支持可采样 RGBA16F 渲染目标及 D32F 深度附件，不满足时返回失败。中间结果每帧生成，不创建源贴图、meta 或 GUID 缓存；场景只保存上述参数，与 IBL 离线贴图资源独立。

## IBL 资源与离线烘焙

当前 IBL 沿用 Ant 的 GGX / split-sum 方案，使用三份独立资源：RGBA16F 漫反射立方图、包含完整粗糙度 mip 链的 RGBA16F 预滤波反射立方图，以及 RG16F BRDF LUT。漫反射图按世界法线采样，预滤波图按反射方向及粗糙度 mip 采样，LUT 以 N·V 和粗糙度为坐标，为材质反射率提供积分系数。它们占用纹理槽 12..14，AO 使用 11，阴影使用 15；项目资源 sampler 仅使用 0..4。

环境组件通过 `ibl_irradiance`、`ibl_prefilter`、`ibl_lut` 保存 GUID，`ibl_enabled`（默认 false）和 `ibl_intensity`（范围 0..1000）控制使用。三项全空表示尚未烘焙，开启开关也不产生 IBL；存在引用时必须三项齐全且格式正确，加载失败明确返回失败，不回退旧场景资源。运行时只消费已生成的线性 KTX1 纹理，资源代际变化时重新加载。

来源需要区分：Ant 提供余弦重要性采样、GGX 预过滤与 split-sum LUT 的数学实现，bgfx 提供跨平台计算、纹理与回读接口；bgfx `18-ibl` 本身消费两张预生成 DDS 立方图，没有这里的独立 BRDF LUT 或 GPU 烘焙流程。来源和改动见 [Ant 烘焙许可](../../third_party/notices/ant-scene-bake.md)。当前按场景保存三张图，后续再将与场景无关的 LUT 改为全局共享资源，尚未实施。

IBL 作业只处理输入环境贴图。场景几何的多反弹 Lightmap 和规则 SH9 探针由同一编辑器插件的 lighting 作业负责；沿用固定 xatlas 和 lightmapper，经核心异步漫反射捕获接口调用 bgfx。详情见[光照烘焙类图](../plugins/editor/class-diagrams/scene-lighting-bake.md)。

编辑器中的输入格式、MCP 作业参数、进度/取消、GUID 发布和预览刷新见[场景烘焙插件](../plugins/editor/scene-bake.md)。运行时只消费发布后的独立纹理、网格与探针资源，不链接烘焙实现。

## 资源生命周期

渲染器按场景所需的 Mesh、Shader、Program 和 Uniform 管理 GPU 资源，Shader 程序和纹理可共享；网格按节点 ID 与修订号更新，避免瞬时几何互相覆盖。`gpu_resource_count()` 统计本系统持有的逻辑句柄，数量由场景资源决定；bgfx 在后续帧异步回收驱动对象。

停止项目、场景替换或准备失败调用可重复的 `release_scene()`，部分初始化同样可清理。玩法会话关闭脚本并释放场景及 GPU 资源，相关 CPU 资源随最后一个共享引用释放；内置定义保留至引擎关闭。完整资源版本热重载重建场景及渲染资源，不自动保存业务状态。

Android 在 Surface 暂停或销毁前由宿主请求关闭渲染器并等待确认，避免后台图形线程访问失效 Surface；恢复后依据仍存活的 CPU 场景重建 GPU 资源。平台验证范围及命令见[验证记录](../testing.md)。

## 能力范围

支持不透明/裁切/排序透明网格、双面材质、分簇多光源、方向/点/聚光阴影、SSR、镜像相机平面反射、普通水材质、Lightmap 与 SH9 探针、IBL/AO、GPU 蒙皮、雾、HDR 中间缓冲与 Bloom，最终输出 SDR。实际验证范围见[验证记录](../testing.md)。尚无 HDR 显示器输出、自动曝光、时序 SSR 降噪、自动局部反射立方探针、逐像素透明排序或动态 GI。

窗口测试覆盖真实后端、空白帧、无光照/PBR 提交、缩放、重复启停、场景替换及延迟回收后 GPU 对象回到待机基线。GPU 测试读取实际截图像素检查 UV、sRGB/线性、动态几何、多灯累加与阴影差异，并验证 PNG 签名、异步截图回执和写入失败。实际执行范围见[验证记录](../testing.md)；软件 Vulkan 验证不代表真机 GPU 性能。

渲染扩展沿用 bgfx 官方示例和已有资源契约；多阶段效果需要明确输入、输出和依赖后再引入渲染图。DCC 视觉制作以 Blender 为基准，USD 转换和材质效果以引擎实际支持范围为准。

普通图片由编辑器依据 `.meta` 调用 BasisU 生成 GUID 缓存，半浮点 KTX1 由 `texture_ktx` 导入器保留线性 HDR 数据。运行时验证 mip 和格式，BasisU 按缓存的格式请求选择设备支持的转码目标，半浮点纹理验证设备能力后直接上传；纹理共享引用跟随材质、环境及 GPU 对象释放。配置与使用说明见[资源管线](../editor/asset-pipeline.md)。

## GPU 蒙皮与共享几何

`AnimationSystem` 采样后通过 MeshRenderer 提交实例独占的列主序位置矩阵与法线逆转置矩阵。`SkinningPass` 每个姿态只计算一次完整顶点流，主场景、AO 深度和全部阴影级联绑定同一输出；材质及项目 Shader 无需专用蒙皮变体。旧模型与新模型使用相同资源和组件契约，核心不包含项目资产名称或按项目选择的兼容分支。

`GpuGeometry` 持有共享不可变源顶点、索引、蒙皮权重和每关节绑定姿态包围盒。场景按 Resource 对象身份使用弱缓存，GpuMesh 实例强持；同 GUID 的新导入版本不会复用旧几何。临时网格由组件实例与网格修订身份区分，不合并可变数据。姿态矩阵缓冲、输出顶点及当前边界属于每个 GpuMesh，角色之间不会共享姿态。最后一个实例释放时销毁几何及持有的资源引用。

GPU 流程参考 bgfx `24-nbody` 的 compute buffer → 绘制输入，以及 MIT Ant 引擎 `pkg/ant.render/skinning/cs_skinning_system.lua` 与 `pkg/ant.resources/shaders/skinning/cs_skinning.sc` 的每实例蒙皮输出。此实现使用固定引擎顶点布局，无通用属性反射；法线使用逆转置矩阵，切线与副切线使用普通矩阵，正交化后重新计算镜像手性。共享几何上传时按非零骨骼影响收集绑定顶点 AABB，每帧变换这些角点并取并集，保守覆盖非负归一权重的线性混合，避免阴影仍使用绑定姿态边界。

compute 使用引擎统一 shaderc 平台配置生成 Metal、DirectX 与 SPIR-V。硬件缺少 compute 或足够绑定槽时明确失败；不增加项目专属 CPU 回退。通用 `set_deformation` 仍支持调用方直接提供动态顶点，但不作为旧模型专用路径。
