# 渲染扩展的上游来源与适配契约

本文件记录已接入渲染扩展的固定来源、资源绑定及正确性边界；当前公共参数和操作见[渲染模块](rendering.md)，实际平台结果与限制集中见[验证记录](../testing.md)。
公开契约与拥有关系以所属类图为准；实现继续使用现有 RenderSystem、组件、资源管线和编辑器烘焙插件。
目标包含 Forward+、局部光阴影、SSR、镜像相机平面反射、透明与折射、静态间接光及动态物体光照探针。

## 固定来源与许可

| 用途 | 固定版本与源码入口 | 许可及采用范围 |
|---|---|---|
| Forward+ 集群构建和剔灯 | [Ant f32fbd4811e90c2433bf159aa40fb8f8aceb8b58](https://github.com/ejoy/ant/tree/f32fbd4811e90c2433bf159aa40fb8f8aceb8b58/pkg/ant.render/render_system)，配合 `pkg/ant.resources/shaders/common/cluster_shading.sh`、`compute/cs_cluster_aabb.sc` | MIT；适配集群划分、光源相交和索引消费，不引入 Ant 的 ECS 或 Lua 调度框架。 |
| 点光、聚光与方向光阴影 | [bgfx 7a70927d2af7ffcc5cef9c9e8f4c821e31fcb099 / 16-shadowmaps](https://github.com/bkaradzic/bgfx/tree/7a70927d2af7ffcc5cef9c9e8f4c821e31fcb099/examples/16-shadowmaps) | BSD-2-Clause；适配光源投影、阴影深度和采样；沿用现有 atlas 管线。 |
| CSM 接收面深度修正 | [Microsoft CascadedShadowMaps11 1ad8f0f6a3e4d9be7e54ca52640ac12b6565ab0c](https://github.com/walbourn/directx-sdk-samples-reworked/tree/1ad8f0f6a3e4d9be7e54ca52640ac12b6565ab0c/CascadedShadowMaps11) | MIT；按 receiver-plane 梯度修正各 PCF 纹素中心的比较深度，减少斜面自阴影条纹。 |
| SSR 遍历和命中验证 | [The Forge 8278d8943f5da12918a990268fc18acdfbce9d9f / SSSR_Intersect.comp.fsl](https://github.com/ConfettiFX/The-Forge/blob/8278d8943f5da12918a990268fc18acdfbce9d9f/Examples_3/Unit_Tests/src/10_ScreenSpaceReflections/Shaders/FSL/SSSR_Intersect.comp.fsl) | 文件同时包含 AMD MIT 和 Forge Apache-2.0 声明；两者均保留，适配 Hi-Z 遍历及命中置信度。 |
| 镜像相机平面反射 | [Three.js bf8f297466d0a8f8ef141d83113c79fadbe1019c / Reflector.js](https://github.com/mrdoob/three.js/blob/bf8f297466d0a8f8ef141d83113c79fadbe1019c/examples/jsm/objects/Reflector.js) | MIT；参考镜像相机、斜近裁剪面、反射纹理投影和递归排除。 |
| 水面流动、反射与折射混合 | [同一 Three.js 版本 / Water2.js](https://github.com/mrdoob/three.js/blob/bf8f297466d0a8f8ef141d83113c79fadbe1019c/examples/jsm/objects/Water2.js) | MIT；适配双法线流动、Fresnel 混合，不引入 Three.js 场景或渲染对象。 |
| 屏幕投影折射、介质吸收 | [同一 Three.js 版本 / transmission_pars_fragment.glsl.js](https://github.com/mrdoob/three.js/blob/bf8f297466d0a8f8ef141d83113c79fadbe1019c/src/renderers/shaders/ShaderChunk/transmission_pars_fragment.glsl.js) | MIT；文件注明算法源于 Khronos glTF Sample Viewer。采用折射向量、投影和 Beer 吸收，保留原归属。 |
| 静态光照 UV 图集 | [xatlas f700c7790aaa030e794b52ba7791a05c085faf0c](https://github.com/jpcy/xatlas/tree/f700c7790aaa030e794b52ba7791a05c085faf0c) | MIT；复用已选依赖生成独立光照 UV 与图集布局，原材质 UV 保留。 |
| 静态表面间接光积分 | [lightmapper 4fd3bf4e2c07263f85d5d875ebdef061bc512dd4](https://github.com/ands/lightmapper/tree/4fd3bf4e2c07263f85d5d875ebdef061bc512dd4) | 公有领域；参考半立方体采样、积分及图像后处理，不将 OpenGL 实现带入运行时。 |
| CPU 球谐编码与求值 | [Ant 670f3d45919841581d705289f5cb3c1238cd8c05 / ant.sh/sh.lua](https://github.com/ejoy/ant/blob/670f3d45919841581d705289f5cb3c1238cd8c05/pkg/ant.sh/sh.lua) | MIT；适配 SH9 系数约定及求值；运行时先在 CPU 为每个 MeshRenderer 插值，再上传 uniform。 |

引入的源码片段须保留原始声明，在 `third_party/notices/` 保存完整许可、固定链接和改动说明。
引用目录中的文件名本身不是版本证据；实际采用文件需与上述固定 commit 对照。
商业、付费或私有参考不作为可提交实现来源；私有项目及资源继续保持忽略。

网页核查同时对照 [GPUOpen SSSR 技术说明](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/stochastic-screen-space-reflections/)、[bgfx 官方样例目录](https://bkaradzic.github.io/bgfx/examples.html)、[Three.js Reflector 文档](https://threejs.org/docs/pages/Reflector.html) 和 [lightmapper 上游说明](https://github.com/ands/lightmapper)。GPUOpen SDK 的现成接入针对 DirectX 12/Vulkan，Three.js 对象依赖其 WebGLRenderer，原始 lightmapper 依赖 OpenGL；本轮复用其算法并适配 bgfx，不能把这些上游的可运行示例当成本引擎 Metal 已支持的证据。

## 现有系统与接入位置

已有材质路径、PBR、IBL、方向光级联阴影、AO、HDR、Bloom、GPU 蒙皮和自定义 Shader 继续使用。
`render_scene.cpp` 组织各通道，`scene_draw.cpp` 共用材质提交，`SceneGpuResources` 持有 GPU 资源及渲染私有通道。
`HdrPostProcessPass` 管理线性颜色、镜面贡献及最终颜色目标；深度附件不被同时采样。
`SurfacePass` 输出共享可采样深度与材质表面数据，`AmbientOcclusionPass` 和 SSR 消费同一输入。
新效果需要明确读取与写入的 GPU 资源，不能仅增加设置开关或 Shader 参数后声称完成。

## 一次性升级 Shader 绑定契约

以下是**已接入的新契约**，替换原来材质可占用 0..10 的约定。
引擎内置、公开范例及本地私有项目 Shader 同步迁移；旧绑定明确报错，不能偷偷覆盖原槽位。

| 槽位 | 用途 |
|---|---|
| 0..4 | 材质的五张显式贴图；各 Shader 可以自行命名，不能越界占用系统输入。 |
| 5 | LightData buffer，保存 Forward+ 光源数据。 |
| 6 | 当前 renderer 的 Lightmap。 |
| 7 | Cluster buffer，保存集群光源索引与范围。 |
| 8 | 当前反射面的 planar reflection 纹理。 |
| 9 | 透明绘制前固定的 opaque scene color。 |
| 10 | 独立可采样的 scene depth。 |
| 11 | AO。 |
| 12..14 | 既有 IBL irradiance、prefilter 和 BRDF LUT。 |
| 15 | 统一 shadow atlas。 |

Shader 的场景纹理声明使用 source 选择来源，由引擎映射到固定槽位；不再允许材质自行选择这些槽位。
资源贴图保留正常 GUID 依赖，场景颜色、深度、反射纹理属于帧内输入，不创建虚构资源路径或资源依赖。
非适用通道绑定约定的安全回退资源并明确可用性，不能继承上一个对象的绑定。
全屏 SSR、Hi-Z 和合成程序可以使用自己的局部绑定布局；它们不会与另一次材质 draw 共用绑定状态。
检查实际 buffer、sampler、framebuffer 附件及 compute 能力，不能只根据槽位数字判断所有平台均支持。

## SSR：复用遍历，保留明确边界

采用 The Forge 的 `InitialAdvanceRay`、`AdvanceRay`、`HierarchicalRaymarch` 与 `ValidateHit` 思路。
移植时保留深度层级步进、起点偏移、背面命中拒绝、线性空间厚度检验及屏幕边缘淡出。
先使用确定性反射射线；去掉 wave occupancy 退出依赖，不默认引入其随机采样、运动矢量和整套时序降噪。
深度层级的 min/max 归约必须与本引擎普通深度方向一致；上游反向深度比较不能原样复制。
输入为当前相机的不透明线性 HDR 颜色、深度、材质处理后的法线、粗糙度与镜面权重。
ASSAO 从深度重建的几何法线不包含法线贴图，不能冒充完整 PBR 法线；也不能给全屏统一粗糙度。
表面预通道或 MRT 输出契约需与材质、顶点变形、alpha cutoff 保持一致，具体形式由类图确定。
未声明所需表面输出的自定义 Shader 应明确不参与 SSR，同时仍能正常渲染自身颜色和深度。
SSR 只采样本帧尚未叠加 SSR 的不透明颜色，不读取本通道正在写入的结果，避免反馈与自反射。
命中颜色通过置信度替换原 IBL 镜面项；不透明主程序以额外 MRT 保存实际已加权的镜面贡献及合成衰减，供准确扣除与替换。漫反射保持原计算，不能简单额外叠加一份高光。
粗糙度控制反射过滤与参与范围，屏幕边缘、离屏对象、遮挡背面和未命中位置回退现有 IBL。
粗糙反射过滤、镜面权重、AO 与雾的作用顺序必须一致，避免反射突然过亮或穿过雾层。
首轮不宣称解决 SSR 固有的离屏缺失，也不以全局提高环境亮度掩盖命中失败。

## 平面反射：镜像相机重新绘制

Three.js `Reflector.js` 的镜像位置、朝向和斜近裁剪面流程作为实现来源，数学移植到现有相机工具。
普通节点上的反射组件定义平面及捕获设置；GPU 纹理由渲染通道拥有，组件只保存可序列化参数。
反射组件所服务的 MeshRenderer 在绘制时获得对应纹理与投影矩阵，材质资源不保存场景节点指针。
镜像相机使用独立视锥及可见性，不能使用主相机的几何剔除结果，否则屏幕外建筑仍然无法出现。
处理镜像产生的绕序变化、透视及正交投影、平面背面、近裁剪偏移和 framebuffer 原点差异。
捕获使用线性 HDR，重用已更新的蒙皮和阴影；不要在每个镜像视角重复推进动画或模拟。
捕获时排除反射面，关闭递归平面捕获、SSR 和需要当前场景颜色的折射，避免循环依赖。
普通透明对象是否进入镜像捕获需明确；首轮以不透明、裁切物体和角色为完整支持范围。
分辨率、可更新平面数和可见性构成显式预算；不通过隐藏的场景名称判断哪些水面生效。
The Forge 的 PPR 依赖屏幕数据重投影，不等同于本节镜像相机方案，不能替代屏幕外倒影支持。

## 透明、水面与折射

透明分类属于材质渲染状态，使用显式 blend mode；现有 alpha cutoff 继续用于裁切。
不透明与裁切先写颜色及深度，透明子网格按视空间深度稳定地从远到近绘制。
透明绘制保留深度测试、关闭深度写入，使用顺序提交避免 bgfx 再按状态重排。
排序不能彻底解决相交透明网格；首轮不引入 OIT，并应记录该表现边界。
水复用普通材质与 Shader 参数，不新增 WaterSystem，也不在 C++ 判断水面或项目材质名称。
采用 Water2 双法线流动和 Fresnel 混合，折射向量与 Beer 吸收参考 transmission Shader 的对应函数。
颜色、IOR、吸收距离、波纹、流速和粗糙度通过既有命名参数设置；贴图沿正常导入与依赖管线加载。
折射使用投影后的场景颜色与深度，检查 UV 越界及前景遮挡，避免把水面前面的物体折进水里。
`gl_FragCoord.w` 不能直接当作跨后端一致的视空间深度，统一采用投影逆变换或显式传递的位置。
场景颜色必须固定为透明绘制前的不透明与 SSR 合成结果，透明对象写入另一张颜色目标。
可采样深度与当前 framebuffer 的深度附件保持分离；不能假设关闭深度写入就允许附件同时被采样。
透明水可同时包含反射、透射和表面覆盖率，避免混合时重复叠加已经采样的背景。
法线扰动后的倒影来自对应平面捕获；未配置平面时采用声明的回退，不能把地面 SSR 像素当作水面 SSR。
Ant `pkg/ant.terrain/assets/shaders/fs_water.sc` 仅作比较：最终输出仍有 TODO/黑色结果，不能作为完整水实现。

## Forward+、局部阴影与静态间接光

Forward+ 将现有逐像素遍历全部光源改为消费相机集群索引；方向光与有限范围局部光分别处理。
复用 Ant 的集群构建与剔灯算法，边界、索引容量、透明对象覆盖和溢出结果必须显式处理。
点光和聚光沿 bgfx 示例建立投影及采样，方向光级联继续保留；结果统一组织到 shadow atlas。
每个点光所需的多个视面、图集边界和过滤半径计入预算，不把六面阴影误算作一个投影视图。
xatlas 负责 UV，lightmapper 负责参考采样和积分；现有 bgfx 编辑器烘焙负责提交场景、读取结果与发布。
lightmapper 的原实现依赖 OpenGL，必须适配到项目固定图形后端，不能把新 OpenGL 上下文作为隐藏依赖。
静态表面光照和探针都消费场景几何及灯光；环境贴图卷积不能替代真正的场景间接光烘焙。
探针先在 CPU 对每个 MeshRenderer 插值 SH9，再以 uniform 提交，避免本轮再增加探针纹理或 buffer 槽。
需统一 SH 系数排列、基函数、余弦卷积与颜色空间，禁止 CPU 和 Shader 采用不同约定。
破坏、移动或移除的静态结构会使烘焙结果失效，适用范围及失效策略必须明确，不能声称动态 GI。
烘焙输出走场景资源子目录、唯一 meta、GUID 缓存和依赖发布，运行时仅消费编译资源。

## GPU 生命周期和迁移验证

流程为：蒙皮；各镜像分别提交其阴影、分簇、捕获；主镜头阴影与分簇；共享深度与表面数据、不透明 HDR、SSR、透明与水、HDR/Bloom、UI。atlas 按完整视图组的 GPU 顺序复用，灯与分簇 buffer 保留各组独立内容。
各通道由 SceneGpuResources 独占，只在调用期间借用场景及输入纹理，不保留跨帧组件指针。
反射和折射的运行时流程只使用 GPU 资源及有序 view，无 CPU readback；离线烘焙按既有作业回读。
resize、切场景、停用效果、部分初始化失败和退出都必须释放或重建对应目标，不引用上一场景内容。
同一帧的读取纹理和写入附件明确分离；视图顺序集中登记，保留 UI 200 与编辑器烘焙 240..255 的范围。
Metal、DirectX 12、Vulkan 保持现有固定后端，分别核查深度范围、纹理原点、格式和资源绑定限制。
后续修改完成受影响的 Debug/Release 构建，仅 Release 执行测试与实际画面检查；反射、透明遮挡及阴影分别验收，未经实测的平台如实标注。
适配范围限于对应渲染功能及直接链路；上游实现与本地来源说明不代表特定画质等级或其他模块的性能结论。
