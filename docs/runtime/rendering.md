# 渲染模块

[渲染类图](class-diagrams/render-system.md) · [场景类图](class-diagrams/scene.md)

[bgfx](https://github.com/bkaradzic/bgfx) 提供跨平台图形接口。macOS / iOS 固定使用 Metal，Windows 使用 DirectX 12，Android / Linux 使用 Vulkan。所选后端不可用时初始化失败，不以 Noop 或其他后端回退代替成功。

渲染阶段、目标缓冲、GPU 资源生命周期和绘制提交的代码组织优先适配上游范例，再接入引擎的节点、组件与资源接口。项目可自定义 Shader、具名参数和材质；框架复用不要求保留范例的固定外观或参数。

## 窗口与场景

编辑器复用同一个 SDL3 窗口。待机时清屏；启动项目后读取世界系统的活动场景，通过活动 Camera 绘制节点上的 MeshRenderer；停止后释放场景和项目 GPU 资源，恢复空白帧。窗口和共享图形设备保留到宿主关闭。

可见物体、相机和光源全部以 `Node + Component` 表达。渲染器不创建隐含的相机、灯光或物体，也不持有跨帧组件指针。一个场景选择一个活动相机，累加最多 64 个启用的方向光、点光或聚光；超出数量返回失败。没有活动相机或启用的 MeshRenderer 时显示空白；没有 Light 时 PBR 不获得直接照明，无光照材质仍可显示。

调用方可向 `prepare_scene(camera)` 和 `render(camera)` 显式提交借用镜头，替代本次绘制使用的活动相机。该镜头可以属于调用方独立场景，渲染器不持有它、不改写项目场景数据；未传入时继续使用项目活动镜头。

节点世界矩阵为列主序，局部 -Z 表示相机朝向和方向光传播方向。相机提供透视视场角或正交竖直半高及近远裁剪面，窗口像素尺寸决定宽高比；视图基忽略缩放大小，避免非均匀缩放扭曲镜头。模型法线使用世界矩阵逆转置；负缩放按行列式切换剔除方向。窗口缩放更新交换链和投影。

## 网格、材质与 Shader

网格由独立资源提供位置、法线、UV0、切线、顶点色和索引；子网格索引范围关联材质槽。动态网格通过 `set_mesh_data` 提交生成模型的深拷贝，修订号变化时更新 GPU。示例 `.mesh` 中的方块有 24 个顶点、36 个索引，每面使用独立顶点；运行时从导入二进制建立 GPU 缓冲。材质和 Shader 通过目录中的唯一逻辑路径定位并按需加载，路径以 `builtin/` 或 `project/` 区分来源。项目材质可复用内置 Shader，内置定义不引用项目资源。

动画等逐帧变形通过 `MeshRenderer::set_deformation` 提交瞬态位置、法线及切线，保留源网格和全部材质槽。渲染器参考 bgfx `35-dynamic` 建立独立动态 VB，变形修订变化时只更新顶点，不重建静态索引缓冲；清除变形恢复源 VB 并释放动态 VB。光照与阴影读取同一份顶点输出，阴影包围盒随变形更新。UV、颜色、索引及子网格范围继续读取源资源；渲染器不执行骨架采样或读取 FBX。

材质实例以名字和数值类型匹配参数，支持 `SetFloat`、`SetV2`、`SetV3`、`SetV4` 与具名 `SetTexture`；未知路径、参数或类型及非法数值返回失败并保留原值。内置材质提供无光照、直接光 PBR 和 Blinn–Phong。PBR 采用 Cook–Torrance 模型中的 GGX 分布、相关 Smith 可见性和 Schlick Fresnel，支持基础色、切线空间法线及 ORM（AO、粗糙度、金属度）贴图，颜色纹理由 sRGB 解码至线性空间，输出经过 Reinhard 色调映射与近似 gamma 编码。

材质资源的 `cull` 为 `back`（默认）、`front` 或 `none`，对应生成模型 `CullMode`。渲染器按每个子网格材质执行面剔除，并处理负缩放；`none` 用于树叶、布片等双面几何。内置 Shader 在背面可见时将法线朝向观察侧。状态由材质实例持有，编辑器保存材质资源时一起保存，不作为 Shader 的固定业务参数。

内置 PBR/Unlit/Blinn–Phong 提供具名 `alphaCutoff` float 参数（0..1，默认 0 关闭）；覆盖率为基础色纹理 alpha、`baseColor.a` 和顶点 alpha 的乘积。低于阈值的片元丢弃，其余按不透明表面写入深度。主 pass 与阴影 pass 共享裁切函数，阴影按子网格取得对应材质的阈值、基础色和纹理；自定义 Shader 可遵守同名 `alphaCutoff` / `baseColor` / `baseColorTexture` 契约。缺少阈值表示不裁切，启用后缺少配套声明则明确失败。这不包含半透明混合与排序。

内置 PBR 的 `specularWorkflow = 1` 启用反射色工作流：`specularColor`（v3）乘 `specularTexture.rgb` 得到线性反射色，粗糙度来自 `1 - smoothness * specularTexture.a`；默认 0 仍走原有 metallic/roughness 与 ORM。SpecGloss 纹理的颜色通道按资源 sRGB 配置解码，alpha 保持线性。`normalScale`（0..8，默认 1）缩放切线空间法线的 XY。`uvTransform`（v4，默认 1,1,0,0）给所有表面纹理提供 scale.xy/offset.zw，阴影裁切使用同一变换；自定义 Shader 缺少该参数时阴影使用原始 UV。

`builtin/shaders/blinn-phong` 提供 Lambert 漫反射与 Blinn–Phong 高光，默认材质为 `builtin/materials/default-blinn-phong`；仍使用基础色、法线、SpecGloss 与发光的命名参数。高光颜色为 `specularColor * specularTexture.rgb`，指数为 `2^(10*smoothness+1)`，不执行 PBR 的能量扣减与漫反射除 π。响应适配 bgfx `18-ibl/fs_ibl_mesh.sc` 的 Lambert、半程向量幂函数与光滑度指数映射；为经验型材质移除其高光归一化、额外 N·L 与 Fresnel，并将指数偏置设为 1。未引入该范例的环境贴图或 IBL，也没有引入外部编辑器专用材质类型。

PBR 反射色流程与 Blinn–Phong 的 `smoothnessSource` 默认 0，光滑度为 `smoothness * specularTexture.a`；设为 1 时直接使用 `baseColorTexture.a * baseColor.a`，不再次乘 smoothness 或顶点 alpha。两个有光照 Shader 的 `specularHighlights` 默认 1，设为 0 关闭直接高光。参数仍由 Shader 资源声明与验证，核心 Material 没有专用字段或算法枚举。

`emissionColor`（v3，0..10000，默认 0）乘 `emissionTexture.rgb` 在线性照明中叠加发光，之后参加雾与色调映射；它不照亮周围对象，也不自动产生 Bloom 或 Light。具名 `fogInfluence`（0..1，默认 1）控制全局雾的影响，天空表面可设 0。

缓冲提交参考固定版本 bgfx `01-cubes`，切线与法线采样参考 `06-bump`；PBR 函数适配 The Forge `Common_3/Graphics/ShaderUtilities.h.fsl`。方向光阴影适配 bgfx `15-shadowmaps-simple`，alpha 阈值 discard 与指数雾分别适配 `16-shadowmaps/fs_shadowmaps_color_texture.sc`、`fs_shadowmaps_color_lighting_main.sh`：裁切改为可配置 alpha，雾改为前向深度、线性颜色与可配置模式，并用负指数避免溢出。可选 ACES 曲线来自 bgfx `shaderlib.sh`。源码保留上游来源，许可位于 `third_party/notices/`。

内置 Shader 由 `cmake/core/render_shaders.cmake` 调用宿主 shaderc 编译并嵌入目标；项目 `.shader` 引用独立源码 GUID，经资源管线生成目标平台二进制。Metal 使用 Metal 程序，DirectX 使用 Shader Model 5，Vulkan 使用 SPIR-V。宿主工具不存在或不可执行时配置失败；`ENGINE_SHADERC_EXECUTABLE` 可显式指定工具。输入及工具未变时复用生成物。

## 渲染设置与帧截图

调用方通过 `RenderSettings` 显式设置环境漫反射、曝光、色调映射、方向光阴影与清屏色。核心不识别编辑器、项目或截图用途；默认值保留原有直接光照行为。环境漫反射为统一颜色和强度，不建立隐藏 Light 节点，也不代表 IBL 或间接光求解。

可选三色环境光按世界法线 Y 在天空、赤道与地面颜色间插值，再乘环境强度。`fog_mode` 可选 none、linear、exponential、exponential_squared。线性雾保留片元到相机的世界距离，从 `fog_start` 到 `fog_end` 混合线性雾色；指数雾与平方指数雾使用相机前向深度减近裁面后的非负距离 d，透过率分别为 `exp(-fog_density*d)` 与 `exp(-(fog_density*d)^2)`，随后进行色调映射和输出编码。fog_density 为非负有限值，0 表示没有衰减。三色环境和雾默认关闭，颜色参数为线性 0..1；它们是明确提交的渲染配置，不属于具体资源来源的特殊分支。

MeshRenderer 的 `cast_shadows` 默认 true；设为 false 后，该组件仍在主 pass 绘制，但完全排除于阴影边界拟合和投射，避免天空模型拉大阴影范围。场景 TOML 同名字段及脚本 `SetCastShadows()` 操作同一生成状态。

项目脚本通过 [Render 静态门面](../plugins/runtime/class-diagrams/render-script-api.md) 在 Gameplay 回调中调用同一设置接口。宿主注入渲染器借用，桌面与 Android 共用；项目把所需数值写在初始化脚本中，重启和热重载重新应用。绑定不自动保存 Scene 数据、不改变默认设置，也不恢复或选择具体项目的风格；一次无效提交保留原有全部设置。脚本清屏色为 `0xRRGGBBAA` 无符号整数，MCP 对应参数为归一化 `[r,g,b,a]` 数组，通道顺序相同。

阴影读取第一个启用的方向光组件，按可绘制模型的世界边界建立正交光源投影。独立阴影 pass 输出 RGBA8 编码深度并使用 D16 附件，主 pass 通过 16 点 PCF 采样可调偏移与分辨率的阴影。坐标变换处理各后端的屏幕原点和深度范围；纹理槽 15 保留给引擎。关闭阴影或移除最后一个方向光后释放专属 GPU 对象，默认关闭时保持原有场景提交次数。当前仅支持第一个方向光阴影，不包含点光、聚光阴影或级联阴影。

`request_screenshot()` 接收调用方提供的绝对 UTF-8 PNG 路径，仅负责排队。宿主继续提交帧，bgfx 回调通过已选 bimg 编码器和 SDL3 IO 写出原始渲染结果，`screenshot_status()` 报告 pending、complete 或 failed。回调正确处理 BGRA、行步长及原点翻转；写入失败不会报告完成。路径选择、目录创建和编辑器相对路径权限检查由调用方负责，核心无需桌面截图权限。

## 资源生命周期

渲染器按场景所需的 Mesh、Shader、Program 和 Uniform 管理 GPU 资源，Shader 程序和纹理可共享；网格按节点 ID 与修订号更新，避免瞬时几何互相覆盖。`gpu_resource_count()` 统计本系统持有的逻辑句柄，数量由场景资源决定；bgfx 在后续帧异步回收驱动对象。

停止项目、场景替换或准备失败调用可重复的 `release_scene()`，部分初始化同样可清理。玩法会话关闭脚本并释放场景及 GPU 资源，相关 CPU 资源随最后一个共享引用释放；内置定义保留至引擎关闭。完整资源版本热重载重建场景及渲染资源，不自动保存业务状态。

Android 在 Surface 暂停或销毁前由宿主请求关闭渲染器并等待确认，避免后台图形线程访问失效 Surface；恢复后依据仍存活的 CPU 场景重建 GPU 资源。平台验证范围及命令见根 [README](../../README.md)。

## 能力范围

支持不透明与 alpha 裁切网格、双面材质、一个活动相机、多材质、基础多光源及单方向光阴影。尚未实现环境贴图、IBL、透明混合与排序、GPU 蒙皮、渲染图或大规模可见性管理。材质 alpha 仅用于明确启用的裁切，不参与混合。

窗口测试覆盖真实后端、空白帧、无光照/PBR 提交、缩放、重复启停、场景替换及延迟回收后 GPU 对象回到待机基线。GPU 测试读取实际截图像素检查 UV、sRGB/线性、动态几何、多灯累加与阴影差异，并验证 PNG 签名、异步截图回执和写入失败。实际执行范围见根 README；软件 Vulkan 验证不代表真机 GPU 性能。

渲染扩展沿用 bgfx 官方示例和已有资源契约；多阶段效果需要明确输入、输出和依赖后再引入渲染图。DCC 视觉制作以 Blender 为基准，USD 转换和材质效果以引擎实际支持范围为准。

图片由编辑器依据 `.meta` 调用 BasisU 生成 GUID 缓存。运行时验证完整 mip 链，再按缓存的格式请求选择设备支持的转码目标，纹理共享引用跟随材质及 GPU 对象释放。配置与使用说明见根 README。
