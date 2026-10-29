# 渲染模块

[渲染类图](class-diagrams/render-system.md) · [场景类图](class-diagrams/scene.md)

[bgfx](https://github.com/bkaradzic/bgfx) 提供跨平台图形接口。macOS / iOS 固定使用 Metal，Windows 使用 DirectX 12，Android / Linux 使用 Vulkan。所选后端不可用时初始化失败，不以 Noop 或其他后端回退代替成功。

## 窗口与场景

编辑器复用同一个 SDL3 窗口。待机时清屏；启动项目后读取世界系统的活动场景，通过活动 Camera 绘制节点上的 MeshRenderer；停止后释放场景和项目 GPU 资源，恢复空白帧。窗口和共享图形设备保留到宿主关闭。

可见物体、相机和光源全部以 `Node + Component` 表达。渲染器不创建隐含的相机、灯光或物体，也不持有跨帧组件指针。一个场景选择一个活动相机，直接光取节点顺序中第一个启用的 `Light`，不累加多个光源。没有活动相机或启用的 MeshRenderer 时显示空白；没有 Light 时 PBR 不获得直接照明，无光照材质仍可显示。

节点世界矩阵为列主序，局部 -Z 表示相机朝向和方向光传播方向。相机提供垂直视场角、近远裁剪面，窗口像素尺寸决定宽高比；视图基忽略缩放大小，避免非均匀缩放扭曲镜头。模型法线使用世界矩阵逆转置；负缩放按行列式切换剔除方向。窗口缩放更新交换链和投影。

## 网格、材质与 Shader

网格由独立资源提供位置、法线和索引。示例 `.mesh` 中的方块有 24 个顶点、36 个索引，每面使用独立顶点；运行时从导入二进制建立 GPU 缓冲。材质和 Shader 通过唯一资源路径注册，路径以 `builtin/` 或 `project/` 区分来源。项目材质可复用内置 Shader，内置定义不引用项目资源。

材质实例以名字和数值类型匹配参数，支持 `SetFloat`、`SetV2`、`SetV3`、`SetV4`；未知路径、参数或类型及非法数值返回失败并保留原值。内置材质提供无光照和直接光 PBR。PBR 采用 Cook–Torrance 模型中的 GGX 分布、相关 Smith 可见性和 Schlick Fresnel，支持线性基础色、金属度及粗糙度，输出经过 Reinhard 色调映射与近似 gamma 编码。

缓冲与提交方式参考固定版本 bgfx `01-cubes`，材质和光照组织参考 `18-ibl`；GGX 分支由引擎实现，不等同于 `18-ibl` 的 Blinn 直接光高光。引擎不依赖示例目录中的模型或环境贴图。

内置 Shader 由 `cmake/render_shaders.cmake` 调用宿主 shaderc 编译并嵌入目标；项目 `.shader` 引用独立源码 GUID，经资源管线生成目标平台二进制。Metal 使用 Metal 程序，DirectX 使用 Shader Model 5，Vulkan 使用 SPIR-V。宿主工具不存在或不可执行时配置失败；`ENGINE_SHADERC_EXECUTABLE` 可显式指定工具。输入及工具未变时复用生成物。

## 资源生命周期

渲染器按场景所需的 Mesh、Shader、Program 和 Uniform 管理 GPU 资源，共享相同资源的节点复用句柄。`gpu_resource_count()` 统计本系统持有的逻辑句柄，数量由场景资源决定；bgfx 在后续帧异步回收驱动对象。

停止项目、场景替换或准备失败调用可重复的 `release_scene()`，部分初始化同样可清理。玩法会话先关闭脚本并释放场景及 GPU 资源，再清理项目定义；内置定义保留至引擎关闭。完整资源版本热重载重建场景及渲染资源，不自动保存业务状态。

Android 在 Surface 暂停或销毁前由宿主请求关闭渲染器并等待确认，避免后台图形线程访问失效 Surface；恢复后依据仍存活的 CPU 场景重建 GPU 资源。平台验证范围及命令见根 [README](../../README.md)。

## 能力范围

支持不透明网格、一个活动相机及一盏方向光。尚未实现阴影、环境贴图、IBL、纹理采样、透明混合与排序、GPU 蒙皮、渲染图或大规模可见性管理。材质 alpha 不参与混合。

窗口测试覆盖真实后端、空白帧、无光照/PBR 提交、缩放、重复启停、场景替换及延迟回收后 GPU 对象回到待机基线。画面效果另通过实际截图检查；软件 Vulkan 验证不代表真机 GPU 性能。

渲染扩展沿用 bgfx 官方示例和已有资源契约；多阶段效果需要明确输入、输出和依赖后再引入渲染图。DCC 视觉制作以 Blender 为基准，USD 转换和材质效果以引擎实际支持范围为准。
