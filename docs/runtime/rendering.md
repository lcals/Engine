# 渲染模块

[渲染类图](class-diagrams/render-system.md) · [场景类图](class-diagrams/scene.md)

[bgfx](https://github.com/bkaradzic/bgfx) 提供跨平台图形接口。macOS / iOS 固定使用 Metal，Windows 使用 DirectX 12，Android / Linux 使用 Vulkan。所选后端不可用时初始化失败，不以 Noop 或其他后端回退代替成功。

渲染阶段、目标缓冲、GPU 资源生命周期和绘制提交的代码组织优先适配上游范例，再接入引擎的节点、组件与资源接口。项目可自定义 Shader、具名参数和材质；框架复用不要求保留范例的固定外观或参数。

## 窗口与场景

编辑器复用同一个 SDL3 窗口。待机时清屏；启动项目后读取世界系统的活动场景，通过活动 Camera 绘制节点上的 MeshRenderer；停止后释放场景和项目 GPU 资源，恢复空白帧。窗口和共享图形设备保留到宿主关闭。

可见物体、相机和光源全部以 `Node + Component` 表达。渲染器不创建隐含的相机、灯光或物体，也不持有跨帧组件指针。一个场景选择一个活动相机，累加最多 64 个启用的方向光、点光或聚光；超出数量返回失败。没有活动相机或启用的 MeshRenderer 时显示空白；没有 Light 时 PBR 不获得直接照明，无光照材质仍可显示。

调用方可向 `prepare_scene(camera)` 和 `render(camera)` 显式提交借用镜头，替代本次绘制使用的活动相机。该镜头可以属于调用方独立场景，渲染器不持有它、不改写项目场景数据；未传入时继续使用项目活动镜头。编辑器预览使用独立的隐藏镜头，后处理仍读取项目场景活动相机的 `PostProcessComponent`，不将预览视角保存到场景。

节点世界矩阵为列主序，局部 -Z 表示相机朝向和方向光传播方向。相机提供透视视场角或正交竖直半高及近远裁剪面，窗口像素尺寸决定宽高比；视图基忽略缩放大小，避免非均匀缩放扭曲镜头。模型法线使用世界矩阵逆转置；负缩放按行列式切换剔除方向。窗口缩放更新交换链和投影。

## 网格、材质与 Shader

网格由独立资源提供位置、法线、UV0、切线、顶点色和索引；子网格索引范围关联材质槽。动态网格通过 `set_mesh_data` 提交生成模型的深拷贝，修订号变化时更新 GPU。示例 `.mesh` 中的方块有 24 个顶点、36 个索引，每面使用独立顶点；运行时从导入二进制建立 GPU 缓冲。材质和 Shader 通过目录中的唯一逻辑路径定位并按需加载，路径以 `builtin/` 或 `project/` 区分来源。项目材质可复用内置 Shader，内置定义不引用项目资源。

动画等逐帧变形通过 `MeshRenderer::set_deformation` 提交瞬态位置、法线及切线，保留源网格和全部材质槽。渲染器参考 bgfx `35-dynamic` 建立独立动态 VB，变形修订变化时只更新顶点，不重建静态索引缓冲；清除变形恢复源 VB 并释放动态 VB。光照与阴影读取同一份顶点输出，阴影包围盒随变形更新。UV、颜色、索引及子网格范围继续读取源资源；渲染器不执行骨架采样或读取 FBX。

材质实例以名字和数值类型匹配参数，支持 `SetFloat`、`SetV2`、`SetV3`、`SetV4` 与具名 `SetTexture`；未知路径、参数或类型及非法数值返回失败并保留原值。内置材质提供无光照、PBR 和 Blinn–Phong。PBR 采用 Cook–Torrance 模型中的 GGX 分布、相关 Smith 可见性和 Schlick Fresnel，支持基础色、切线空间法线及 ORM（AO、粗糙度、金属度）贴图，颜色纹理由 sRGB 解码至线性空间。受光照材质叠加直接光、环境光与启用的 IBL；无光照材质不计算这些照明。HDR 关闭时，受光照材质在表面 Shader 内执行曝光、Reinhard 或可选 ACES 色调映射，无光照材质跳过这两步，再各自近似 gamma 编码输出到 SDR。HDR 开启时，全部场景材质先输出线性 RGBA16F，最后统一执行曝光、色调映射和 gamma 编码；最终屏幕仍为 SDR。

材质资源的 `cull` 为 `back`（默认）、`front` 或 `none`，对应生成模型 `CullMode`。渲染器按每个子网格材质执行面剔除，并处理负缩放；`none` 用于树叶、布片等双面几何。内置 Shader 在背面可见时将法线朝向观察侧。状态由材质实例持有，编辑器保存材质资源时一起保存，不作为 Shader 的固定业务参数。

内置 PBR/Unlit/Blinn–Phong 提供具名 `alphaCutoff` float 参数（0..1，默认 0 关闭）；覆盖率为基础色纹理 alpha、`baseColor.a` 和顶点 alpha 的乘积。低于阈值的片元丢弃，其余按不透明表面写入深度。主 pass 与阴影 pass 共享裁切函数，阴影按子网格取得对应材质的阈值、基础色和纹理；自定义 Shader 可遵守同名 `alphaCutoff` / `baseColor` / `baseColorTexture` 契约。缺少阈值表示不裁切，启用后缺少配套声明则明确失败。这不包含半透明混合与排序。

内置 PBR 的 `specularWorkflow = 1` 启用反射色工作流：`specularColor`（v3）乘 `specularTexture.rgb` 得到线性反射色，粗糙度来自 `1 - smoothness * specularTexture.a`；默认 0 仍走原有 metallic/roughness 与 ORM。SpecGloss 纹理的颜色通道按资源 sRGB 配置解码，alpha 保持线性。`normalScale`（0..8，默认 1）缩放切线空间法线的 XY。`uvTransform`（v4，默认 1,1,0,0）给所有表面纹理提供 scale.xy/offset.zw，阴影裁切使用同一变换；自定义 Shader 缺少该参数时阴影使用原始 UV。

`builtin/shaders/blinn-phong` 提供 Lambert 漫反射与 Blinn–Phong 高光，默认材质为 `builtin/materials/default-blinn-phong`；仍使用基础色、法线、SpecGloss 与发光的命名参数。直接光高光颜色为 `specularColor * specularTexture.rgb`，指数为 `2^(10*smoothness+1)`，不执行 PBR 的能量扣减与漫反射除 π。直接光响应适配 bgfx `18-ibl/fs_ibl_mesh.sc` 的 Lambert、半程向量幂函数与光滑度指数映射；为经验型材质移除其高光归一化、额外 N·L 与 Fresnel，并将指数偏置设为 1。环境照明复用本引擎的 GGX split-sum IBL 路径，不等同于该范例的完整材质模型。

PBR 反射色流程与 Blinn–Phong 的 `smoothnessSource` 默认 0，光滑度为 `smoothness * specularTexture.a`；设为 1 时直接使用 `baseColorTexture.a * baseColor.a`，不再次乘 smoothness 或顶点 alpha。两个有光照 Shader 的 `specularHighlights` 默认 1，设为 0 关闭直接高光。参数仍由 Shader 资源声明与验证，核心 Material 没有专用字段或算法枚举。

`emissionColor`（v3，0..10000，默认 0）乘 `emissionTexture.rgb` 在线性照明中叠加发光，之后参加雾与色调映射；它不照亮周围对象，也不创建 Light。HDR 与 Bloom 默认关闭，显式开启后，高亮发光与其他高亮场景颜色共同按线性阈值参与泛光。具名 `fogInfluence`（0..1，默认 1）控制全局雾的影响，天空表面可设 0。

缓冲提交参考固定版本 bgfx `01-cubes`，切线与法线采样参考 `06-bump`；PBR 函数适配 The Forge `Common_3/Graphics/ShaderUtilities.h.fsl`。方向光阴影的 packed-depth 与 PCF 适配 bgfx `15-shadowmaps-simple`，级联划分与稳定投影参考 `16-shadowmaps`。alpha 阈值 discard 与指数雾分别适配 `16-shadowmaps/fs_shadowmaps_color_texture.sc`、`fs_shadowmaps_color_lighting_main.sh`：裁切改为可配置 alpha，雾改为前向深度、线性颜色与可配置模式，并用负指数避免溢出。可选 ACES 曲线来自 bgfx `shaderlib.sh`。源码保留上游来源，许可位于 `third_party/notices/`。

内置 Shader 由 `cmake/core/render_shaders.cmake` 调用宿主 shaderc 编译并嵌入目标；项目 `.shader` 引用独立源码 GUID，经资源管线生成目标平台二进制。Metal 使用 Metal 程序，DirectX 使用 Shader Model 5，Vulkan 使用 SPIR-V。宿主工具不存在或不可执行时配置失败；`ENGINE_SHADERC_EXECUTABLE` 可显式指定工具。输入及工具未变时复用生成物。

## 渲染设置与帧截图

场景环境光、IBL、雾与清屏色由普通节点的 `EnvironmentComponent` 持有；曝光、色调映射、屏幕空间 AO、HDR 与 Bloom 由活动相机同节点的 `PostProcessComponent` 持有；方向光阴影参数由 `Light` 持有。一个场景最多允许一个启用环境和一个启用投影的方向光，重复时明确失败。渲染每帧读取组件，不跨帧持有组件指针；缺少或禁用组件时使用固定默认值，切场景不继承前一个场景的外观。

`RenderSettings` 是这些组件组成的瞬时聚合快照，不是另一份场景配置。`RenderSystem::set_settings()` 只更新已有组件，缺少变更域的组件时失败，不创建节点；无场景时只修改待机窗口配置，该配置不影响以后加载的场景。默认环境光强度为 0，IBL、AO、HDR、Bloom 与阴影关闭，曝光为 1，使用 Reinhard。核心不识别调用方是编辑器、项目还是截图工具。

可选三色环境光按世界法线 Y 在天空、赤道与地面颜色间插值，再乘环境强度。常量/三色环境光与 IBL 独立，可同时贡献照明；调试曝光时需一并检查二者，避免重复补光使画面发灰。`fog_mode` 可选 none、linear、exponential、exponential_squared。线性雾保留片元到相机的世界距离，从 `fog_start` 到 `fog_end` 混合线性雾色；指数雾与平方指数雾使用相机前向深度减近裁面后的非负距离 d，透过率分别为 `exp(-fog_density*d)` 与 `exp(-(fog_density*d)^2)`，随后进行输出处理。fog_density 为非负有限值，0 表示没有衰减。三色环境和雾默认关闭，颜色参数为线性 0..1；这些值属于环境组件，不含具体资源来源的特殊分支。

MeshRenderer 的 `cast_shadows` 默认 true；设为 false 后，该组件仍在主 pass 绘制，但完全排除于阴影边界拟合和投射，避免天空模型拉大阴影范围。场景 TOML 同名字段及脚本 `SetCastShadows()` 操作同一生成状态。

项目初值保存到 `.scene`。脚本通过 `Node.GetComponent` / `AddComponent` 访问组件，或在 Gameplay 回调中使用 [Render 静态门面](../plugins/runtime/class-diagrams/render-script-api.md) 修改已有组件；脚本绑定不创建节点，也不自动写入磁盘。宿主注入渲染器借用，桌面与 Android 共用；一次无效提交保留原有全部设置。编辑器 MCP `set_render_settings` 可按需创建环境节点和相机后处理组件，修改进入撤销历史及未保存状态，再由 `save_scene` 落盘。曝光、AO、HDR 与 Bloom 修改要求已有活动相机，开启阴影要求已有方向光。脚本清屏色为 `0xRRGGBBAA` 无符号整数，MCP 对应参数为归一化 `[r,g,b,a]` 数组，通道顺序相同。

方向光阴影读取唯一启用 `cast_shadows` 的 `Light`，支持 1、2、4 级 CSM；默认 4 级、阴影距离 150、对数/均匀划分混合比例 0.6、级联交界混合比例 0.1。对应场景字段为 `shadow_cascades`、`shadow_distance`、`shadow_split_lambda`、`shadow_cascade_blend`。距离范围为 `(0, 100000]`，两项比例范围分别为 `[0, 1]`、`[0, 0.3]`；实际远端取相机远裁面与阴影距离的较小值，远端不超过近裁面时不提交阴影。显式传入的观察镜头决定级联范围，预览和玩法共用同一实现。逐灯配置归组件，渲染器不创建场景节点或保存编辑器策略。

每级覆盖镜头的一段视锥，使用包围球固定正交范围并将中心对齐纹素；深度范围纳入与本级范围相交的投射物，避免仅按接收面裁掉投影。阴影 pass 输出 RGBA8 编码深度并使用 D16 附件。所有级联共用一张 atlas，`shadow_resolution` 表示完整 atlas 边长：单级占整图，两级各占一个半边长 tile，四级占满 2×2；4096 的四级 atlas 每级为 2048。View 0..3 留给阴影，4/5 分别为 AO 深度预通道与计算，主场景使用 View 6；启用 HDR 时，View 6 写入线性 RGBA16F，7..9 完成后处理，UI 随后使用 View 200。编辑器 GPU 作业保留 View 240..255。阴影仍只占用纹理槽 15。

主 pass 按观察镜头的正向深度选级，16 点 PCF 的每次采样限制在本级 tile 内。每级末尾的混合区与下一层平滑过渡，最后一级在同样区间淡出；混合比例为 0 时使用硬切换。下一级投影向前扩展以覆盖混合区。坐标变换处理各后端的屏幕原点和深度范围；超出有效投影或阴影距离返回全可见。没有投影方向光时释放专属 GPU 对象；聚合设置查询和修改此时选择首个启用方向光，逐灯编辑应直接使用 `Light`。目前不包含点光或聚光阴影。

CSM 参考实际依赖版本的 [bgfx `16-shadowmaps`](https://github.com/bkaradzic/bgfx/tree/7a70927d2af7ffcc5cef9c9e8f4c821e31fcb099/examples/16-shadowmaps)（commit `7a70927d2af7ffcc5cef9c9e8f4c821e31fcb099`）：`shadowmaps.cpp` 提供混合划分和纹素稳定思路，`fs_shadowmaps_color_lighting_main.sh` 提供分级采样结构。引擎适配为单 atlas、相机深度选级、包围球稳定范围、交界重叠混合和末级淡出，PCF 保留 `15-shadowmaps-simple/fs_sms_shadow.sh` 的 4×4 布局，并增加 tile 边界限制。Dario Manesku 的示例版权与 BSD-2-Clause 条款保留在 [bgfx 许可](../../third_party/notices/bgfx.txt)。

`shadow_bias` 是共用的归一化深度偏移。级联距离、投射物深度范围和场景尺度改变后，应重新对比斜面自阴影与接触阴影；偏移过小可能出现条纹，过大可能使投影脱离物体。纹素对齐和交界混合不代替偏移调整，也不保证任意运动下完全无闪烁。

`request_screenshot()` 接收调用方提供的绝对 UTF-8 PNG 路径，仅负责排队。宿主继续提交帧，bgfx 回调通过已选 bimg 编码器和 SDL3 IO 写出原始渲染结果，`screenshot_status()` 报告 pending、complete 或 failed。回调正确处理 BGRA、行步长及原点翻转；写入失败不会报告完成。路径选择、目录创建和编辑器相对路径权限检查由调用方负责，核心无需桌面截图权限。

## 屏幕空间环境遮蔽

AO 适配固定 bgfx commit `7a70927d2af7ffcc5cef9c9e8f4c821e31fcb099` 的 [39-assao](https://github.com/bkaradzic/bgfx/tree/7a70927d2af7ffcc5cef9c9e8f4c821e31fcb099/examples/39-assao)，使用固定 Q2 与两次边缘保持模糊。实现和 GPU 资源封装于渲染私有 `AmbientOcclusionPass`，不增加场景节点或框架系统。View 4 用现有材质程序提交仅写深度的预通道，保留自定义顶点变形、材质剔除和 alpha 裁切；View 5 顺序计算深度、法线、遮蔽及模糊，View 6 的主场景采样最终结果，启用 HDR 时随后执行 View 7..9 后处理。UI 与编辑器 GPU 作业继续使用 200 与 240..255。

`PostProcessData` 保存 `ao_enabled`（默认 false）、`ao_radius`（默认 1，范围 `(0,100]`）、`ao_strength`（默认 1，范围 `[0,2]`）、`ao_power`（默认 1.5，范围 `[0.1,8]`）和 `ao_fade_start` / `ao_fade_end`（默认 80/150，要求 `0 <= start < end <= 100000`）。半径与淡出距离使用世界单位，参数必须有限；关闭开关保留其他设置。参数由活动场景相机提供，预览隐藏镜头只决定本帧投影与深度，支持透视和正交相机。MCP 的同名驼峰字段和脚本 `SetAmbientOcclusion` 复用组件校验与持久数据；MCP 编辑进入撤销链，再由 `save_scene` 保存，脚本修改不自动写盘。不维护单独的编辑器效果状态。

AO 只衰减内置受光照材质的环境光与 IBL；直射光、自发光、无光照材质、天空和 UI 不受屏幕空间 AO 压暗。项目 Shader 可按 `s_ambient_occlusion` 与 `u_ambient_occlusion` 契约采样，保留纹理槽 11；IBL 使用 12..14、阴影使用 15，项目材质 sampler 限于 0..10，导入、运行时资源校验和反射遵守同一规则。

开启时按窗口尺寸准备深度、法线及 AO 中间纹理，关闭、场景释放、设备重建或失败时清理。结果每帧重新计算，只存在于 GPU，不生成贴图源文件、meta 或 GUID 资源，也不依赖离线烘焙；场景只保存 AO 参数。所需计算或纹理能力不可用时明确返回失败。它使用当前可见深度，不替代场景几何烘焙或屏幕外遮挡信息。

Metal 能力检查沿用上游实际访问契约：本通道的 R16F、RGBA8、RG8、R8 仅使用分离的 `read` / `write` 绑定，不要求单个 Shader 的 `read_write` 访问。bgfx Metal 的 `IMAGE_READ/WRITE` 标志来自后者的 tier，不能据此拒绝可用的 RG8；仍检查 compute、纹理数组、绑定数量及 D32F 附件能力。其他后端保留独立 image 能力检查。

2026-10-31 已完成 macOS Metal 的 Debug / Release 定向验证，覆盖参数与序列化、脚本与 MCP、资源导入，以及实际 GPU 的透视/正交 AO、非对齐窗口缩放和开关资源释放。无间接光时开关 AO 的截图一致，关闭 AO 后恢复原始像素；神庙按固定七点路线复拍五个机位。该结果不代表 Windows / Android 已验证，也不作为全部运动帧无闪烁的保证。

## HDR 场景合成与 Bloom

HDR 与 Bloom 适配固定 bgfx commit `7a70927d2af7ffcc5cef9c9e8f4c821e31fcb099` 的 [09-hdr](https://github.com/bkaradzic/bgfx/tree/7a70927d2af7ffcc5cef9c9e8f4c821e31fcb099/examples/09-hdr)，使用其全屏绘制和多通道组织、九次采样亮部提取、纵向 `blur9`、横向 `blur9` 与合成流程。保留 `blur9` 权重 `1 / 0.9 / 0.55 / 0.18 / 0.1` 和 `4.46` 归一化；适配为中心对称 3×3 提取核、线性 RGBA16F 中间值、一次最终输出转换及引擎资源生命周期。源码、调整说明和 BSD-2-Clause 许可见 [HDR 来源](../../third_party/notices/bgfx-hdr.md)。

`HdrPostProcessPass` 是渲染私有实现，不增加场景节点或独立框架系统。HDR 开启时，View 6 将场景表面、天空与 3D 文字绘制到全尺寸 RGBA16F 颜色和 D32F 深度目标；Bloom 开启时，View 7 将九次采样的平均线性颜色减去阈值并保留非负结果，输出到半尺寸 RGBA16F 目标，View 8 纵向模糊至八分之一尺寸目标。View 9 水平模糊 Bloom 并按强度加回场景，最后统一执行显式 `exposure`、Reinhard/ACES 和 gamma 编码。尺寸按窗口像素向上取整，两轴模糊间距都以八分之一目标的纹素尺寸乘半径倍率计算。Bloom 关闭而 HDR 开启时跳过 View 7/8，仅执行最终输出转换。

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

Bloom 与 HDR 的依赖严格校验，不自动开启或关闭另一项。MCP 可在一次 `set_render_settings` 中提交 `{"hdrEnabled":true,"bloomEnabled":true}` 或 `{"bloomEnabled":false,"hdrEnabled":false}`，全部通过后才修改组件，支持撤销、重做与 `save_scene`。非法组合、类型、范围或非有限数值整次失败，保持原设置。

脚本静态入口为 `Render.SetHdr(enabled)` 与 `Render.SetBloom(enabled, threshold = 1, intensity = 0.15, radius = 1)`；`PostProcessComponent` 提供同签名方法。先开 HDR 再开 Bloom，先关 Bloom 再关 HDR；每次检查 bool 返回值，顺序错误返回 false 并保留全部设置。旧曝光和 AO 接口保留 HDR/Bloom 字段，关闭 HDR/Bloom 不改变其他后处理参数。脚本操作不自动保存场景。

窗口尺寸变化时重建中间目标；关闭 Bloom 释放两组 Bloom 目标，关闭 HDR、场景释放、设备重建或准备失败时释放整套专属 GPU 资源。设备必须支持可采样 RGBA16F 渲染目标及 D32F 深度附件，不满足时返回失败。中间结果每帧生成，不创建源贴图、meta 或 GUID 缓存；场景只保存上述参数，与 IBL 离线贴图资源独立。

2026-10-31 已完成 macOS Metal 的 Debug / Release 构建，受影响的 10 项定向测试均通过；字体/UI 测试修正示例场景新增环境节点后的旧数量断言后单独复跑。实际 GPU 验证覆盖自发光几何外的局部 Bloom、强度归零及高阈值恢复纯 HDR 画面、奇数尺寸缩放、关闭 HDR 恢复原 SDR 像素、3D 字体与 UI 同帧绘制以及停止后的资源释放。神庙在保持原曝光、AO、光照和阴影参数下，开启 HDR/Bloom，使用阈值 1、强度 0.15、半径 1，按固定七点路线完成五机位对比；保存只更新场景资源，复用既有材质和网格。本轮未运行 Windows / Android 验证。

## IBL 资源与离线烘焙

当前 IBL 沿用 Ant 的 GGX / split-sum 方案，使用三份独立资源：RGBA16F 漫反射立方图、包含完整粗糙度 mip 链的 RGBA16F 预滤波反射立方图，以及 RG16F BRDF LUT。漫反射图按世界法线采样，预滤波图按反射方向及粗糙度 mip 采样，LUT 以 N·V 和粗糙度为坐标，为材质反射率提供积分系数。它们占用纹理槽 12..14，AO 使用 11，阴影使用 15；项目材质仅使用 0..10。

环境组件通过 `ibl_irradiance`、`ibl_prefilter`、`ibl_lut` 保存 GUID，`ibl_enabled` 和 `ibl_intensity` 控制使用。三项全空表示尚未烘焙，开启开关也不产生 IBL；存在引用时必须三项齐全且格式正确，加载失败明确返回失败，不回退旧场景资源。运行时只消费已生成的线性 KTX1 纹理，资源代际变化时重新加载。

GPU 烘焙归 `plugins/editor/scene_bake/`：MCP 启动作业、查询进度或取消，主线程按帧提交计算和回读。输入为已导入的经纬全景图或立方图，生成贴图与唯一 `.meta` 放在场景旁的同名目录，转换缓存仍为 `cache/gen/<完整GUID>`。完成发布后更新场景引用并刷新预览，保留原开关与强度；只有作业 `complete` 才表示全部完成。计算与读回异步推进，发布后的资源导入及预览重建仍为同步流程。

来源需要区分：Ant 提供余弦重要性采样、GGX 预过滤与 split-sum LUT 的数学实现，bgfx 提供跨平台计算、纹理与回读接口；bgfx `18-ibl` 本身消费两张预生成 DDS 立方图，没有这里的独立 BRDF LUT 或 GPU 烘焙流程。来源和改动见 [Ant 烘焙许可](../../third_party/notices/ant-scene-bake.md)。当前按场景保存三张图，后续再将与场景无关的 LUT 改为全局共享资源，尚未实施。

烘焙仅处理输入环境贴图，不采集场景几何，不代表局部反射探针、Lightmap 或全局光照烘焙。HDR 输入目前支持线性半浮点 KTX1，不直接导入 `.hdr` / `.exr`。完整流程和能力要求见 [README](../../README.md#场景-gpu-ibl-烘焙) 与[场景烘焙设计](../plugins/editor/class-diagrams/scene-bake.md)。

## 资源生命周期

渲染器按场景所需的 Mesh、Shader、Program 和 Uniform 管理 GPU 资源，Shader 程序和纹理可共享；网格按节点 ID 与修订号更新，避免瞬时几何互相覆盖。`gpu_resource_count()` 统计本系统持有的逻辑句柄，数量由场景资源决定；bgfx 在后续帧异步回收驱动对象。

停止项目、场景替换或准备失败调用可重复的 `release_scene()`，部分初始化同样可清理。玩法会话关闭脚本并释放场景及 GPU 资源，相关 CPU 资源随最后一个共享引用释放；内置定义保留至引擎关闭。完整资源版本热重载重建场景及渲染资源，不自动保存业务状态。

Android 在 Surface 暂停或销毁前由宿主请求关闭渲染器并等待确认，避免后台图形线程访问失效 Surface；恢复后依据仍存活的 CPU 场景重建 GPU 资源。平台验证范围及命令见根 [README](../../README.md)。

## 能力范围

支持不透明与 alpha 裁切网格、双面材质、一个活动相机、多材质、基础多光源、一个方向光的 1/2/4 级阴影、常量/三色环境光、IBL、屏幕空间 AO、雾、曝光和色调映射，以及可选线性 HDR 场景合成与 Bloom，最终输出 SDR。尚未实现 HDR 显示器输出、自动曝光、局部光源阴影、场景几何反射探针、Lightmap、透明混合与排序、GPU 蒙皮、渲染图或大规模可见性管理。材质 alpha 仅用于明确启用的裁切，不参与混合。

窗口测试覆盖真实后端、空白帧、无光照/PBR 提交、缩放、重复启停、场景替换及延迟回收后 GPU 对象回到待机基线。GPU 测试读取实际截图像素检查 UV、sRGB/线性、动态几何、多灯累加与阴影差异，并验证 PNG 签名、异步截图回执和写入失败。实际执行范围见根 README；软件 Vulkan 验证不代表真机 GPU 性能。

渲染扩展沿用 bgfx 官方示例和已有资源契约；多阶段效果需要明确输入、输出和依赖后再引入渲染图。DCC 视觉制作以 Blender 为基准，USD 转换和材质效果以引擎实际支持范围为准。

普通图片由编辑器依据 `.meta` 调用 BasisU 生成 GUID 缓存，半浮点 KTX1 由 `texture_ktx` 导入器保留线性 HDR 数据。运行时验证 mip 和格式，BasisU 按缓存的格式请求选择设备支持的转码目标，半浮点纹理验证设备能力后直接上传；纹理共享引用跟随材质、环境及 GPU 对象释放。配置与使用说明见根 README。
