# 渲染模块

[类图](class-diagrams/render-system.md) · [场景类图](class-diagrams/scene.md)

[bgfx](https://github.com/bkaradzic/bgfx) 提供跨平台渲染。图形后端按平台固定：macOS / iOS 使用 Metal，Windows 使用 DirectX 12，Android / Linux 使用 Vulkan。所选后端初始化失败时返回失败，不接受 Noop 或其他后端回退。

## 当前最小场景阶段

编辑器复用同一个 SDL3 窗口。默认只清屏显示空白；项目启动后，渲染器读取世界系统的活动场景，用场景相机绘制节点上的 MeshRenderer。停止项目后销毁场景与项目 GPU 资源，继续提交空白帧，避免最后一帧残留。窗口和共享图形设备的生命周期独立于项目运行。

场景中的可见物体、相机和灯光全部为 `Node + Component`：`MeshRenderer`、`Camera`、`Light` 都由所属节点拥有。渲染器不创建隐藏相机、灯光或方块对象，也不持有跨帧的组件指针。本轮场景只有一个活动相机，直接光采用节点顺序中第一个启用的 `Light`；其他 Light 尚不累加。无相机或没有启用的 MeshRenderer 时显示空白，无 Light 时 PBR 不得到直接照明，unlit 材质仍可显示。

节点世界矩阵使用列主序，局部 `-Z` 为相机朝向和方向光传播方向。Camera 提供垂直视场角、近远裁剪面，实际窗口像素尺寸决定宽高比；视图使用世界 -Z 和 +Y 正交化基，忽略缩放大小，避免非均匀缩放扭曲镜头。模型法线使用世界矩阵的逆转置，因此非均匀缩放不会直接破坏光照方向；负缩放根据行列式切换背面剔除方向。窗口缩放会更新交换链及投影。

内置方块每面使用独立顶点，合计 24 顶点、36 索引；数据包含位置和平面法线。本轮只提供不透明方块，Material 的基础色为线性 RGB，alpha 暂不参与混合。材质可选 unlit；PBR 分支使用 Cook–Torrance 直接光模型（GGX 分布、相关 Smith 可见性、Schlick Fresnel），支持金属度和粗糙度，输出经过 Reinhard 色调映射及近似 gamma 编码。尚无阴影、环境贴图、IBL、纹理采样或透明排序。

实现参考固定版本 bgfx `01-cubes` 的缓冲与提交方式、`18-ibl` 的材质和光照组织。`18-ibl` 自身的直接光高光采用 Blinn，本项目的 GGX 分支独立实现，不能将两者等同。本轮不读取范例的模型或环境贴图。

`cmake/render_shaders.cmake` 使用宿主 `shaderc` 编译两个 Shader 并以 `--bin2c` 嵌入目标：macOS/iOS 使用 Metal，Windows 使用 DirectX Shader Model 5，Android/Linux 使用 SPIR-V。缺失或不能执行宿主工具时配置失败；可以通过 `ENGINE_SHADERC_EXECUTABLE` 指定工具。命令、安装入口及实际验证范围统一见根 README。每个 Shader 仅在输入或工具改变后重新生成；普通构建不重新处理上游依赖。

每个活动场景最多拥有一组内置 Mesh/Shader 资源，所有方块节点复用这组 GPU 对象；材质数据每次提交设置。目前完整资源组包含 2 个缓冲、2 个 Shader、1 个 Program、6 个 Uniform，共 11 个逻辑句柄。`gpu_resource_count()` 统计本系统拥有的句柄；bgfx 在后续帧异步回收驱动资源。停止与准备失败统一调用 `release_scene()`，允许重复及部分初始化；场景编号变化触发旧资源释放和新资源准备，覆盖热重载场景替换。

Android 原有 Surface 暂停协议继续保留：关闭 bgfx 前释放场景 GPU 资源，恢复时从仍存活的 CPU 场景重建。本轮先验证桌面，不能把代码覆盖 Android 路径视为 Android 已完成实际验证。

窗口 GPU 测试覆盖空白帧、unlit/PBR 绘制提交、缩放、三轮启动停止、场景替换、同一个窗口保持，以及若干帧后 bgfx 的缓冲、Shader、Program、Uniform 数量回到待机基线。测试统计不能证明画面观感，最终还需编辑器实际窗口截图和人工审核。

## 后续渲染方向

以下保留既定渲染方向，均不属于当前硬编码方块里程碑已经完成的能力。

- 引擎使用 PBR，材质系统参考 Unity 和 Unreal Engine，并内置默认材质；bgfx 负责底层渲染。效果优先参考并复用 [bgfx 官方示例](https://bkaradzic.github.io/bgfx/examples.html)。
- 完整光照阶段覆盖环境光、主光源与阴影；局部光源按游戏需求扩展，全局光照后续考虑。效果与方案参考 `references/`。
- 以 Blender 作为当前 DCC 视觉制作模板。USD 导入后，在约定支持的材质与渲染效果范围内追求与 Blender 预览一致，最终效果以引擎预览为准。
- 多阶段渲染流程使用渲染图组织输入、输出和依赖，由 bgfx 实际执行。当前单次直接光方块提交不提前引入通用渲染图框架；后续 PBR 多阶段流程需要时再接入。
- 后期与渲染优化优先复用 bgfx 能力和官方示例方案；渲染侧需要支持 [GPU 蒙皮](animation.md)、大规模单位提交与可见性管理，配合 ECS、导航、物理和行为更新。
- 材质、贴图与 Shader 热重载用于编辑器运行期间调整视觉效果；资源变化、缓存更新与替换失败处理见[资源管线](../editor/asset-pipeline.md)。当前实现的是脚本替换后重建场景，不等同于完整渲染资源热重载。
