# 大气天空与 Hex-Tiling

[类图与参数](class-diagrams/atmosphere-hex-material.md) ·
[环境组件](class-diagrams/environment-components.md) · [渲染 MCP](../plugins/editor/class-diagrams/render-mcp-tools.md)

使用仓库固定的 bgfx API 167 上游算法，接入现有环境组件、标准 PBR 和 HDR 管线。本文说明现行职责和适配边界，实际验收与平台范围见[测试文档](../testing.md)。

## 职责

- `EnvironmentComponent` 保存大气参数和方向太阳的场景局部引用，schema 是唯一持久模型。
- `AtmospherePass` 拥有程序和 LUT，由 `SceneGpuResources` 管理其场景与设备生命周期。
- 渲染设置读取当前组件；主视图、反射和捕获分别使用真实观察矩阵，纯天空场景也可绘制。
- Hex 由 `Material` 的具名参数开启，公共 PBR surface 采样同时供主颜色与 Surface/MRT 使用。
- 场景组件和材质的编辑、保存、撤销、TS 与 MCP 共用原有链路，不存另一份全局效果状态。
- 来源为 bgfx `53-sky2`、`49-hextile` 和固定 MIT 表面梯度法线参考；固定版本与许可见[渲染上游来源](rendering-sources.md)。

## 大气渲染

1. 采用上游 Transmittance `256×64`、Multiscatter `32×32` 和 Sky-view `192×108`，均为线性
   `RGBA16F`，保留上游积分、相函数和 LUT 映射。Compute 后按 bgfx 的独立 View 顺序消费，
   具体 ViewId 统一在 `render_constants.hpp` 登记，不把示例的 0..5 原样搬入。
2. ViewId 统一在 `render_constants.hpp` 登记：Transmittance/Multiscatter 为 1/2，主视图
   Sky-view 为 109，捕获 Sky-view 为 239；反射使用各自视图组。LUT 生成先于对应消费视图，
   不与 UI 及编辑器烘焙作业冲突。
3. 场景米单位转上游 km：`altitude_km=(world_y-ground_height)*0.001`。明确近地面适用范围，
   地下高度按上游近地面保护处理；不宣称支持轨道/太空相机或任意星球中心的全局场景。
4. 天空全屏绘制必须写入原线性 HDR 颜色目标，并将 MRT 的环境高光替换量置零，不写几何
   Surface/深度/法线。背景几何置于该后端的远深度并启用对应深度比较、禁止深度写入，确保
   bgfx 排序不会使天空覆盖已画物体；若使用独立 View，则清理只能发生一次且须在背景之前。
   保留实际相机逆矩阵、平台深度约定和纹理原点；镜像观察使用自身矩阵。
5. **不能原样输出上游 `sky2_tonemap()`**：其背景 Shader 末尾已经做了色调映射，本引擎有统一的
   `HdrPostProcessPass`。适配后背景输出线性散射/日盘值，由现有曝光、Bloom 和显示 resolve
   处理一次，HDR 捕获仍读取曝光/显示转换前的数据。不能为匹配截图叠加 gamma 或强度补偿。
6. 支持实时天空和太阳方向/颜色联动；天空变化本身不等于物体已得到实时天空间接照明。
   不搬示例的近似环境项或地形 Shader；空气透视、逐表面太阳透过率及动态 IBL 尚未实现。
   已有静态 IBL、Lightmap、SH 探针和雾继续各自配置，避免暗中替换光照来源。
7. 反射应能看到天空，离线烘焙捕获应能看到与捕获相机匹配的天空；复用现有捕获/烘焙作业，
   不建另一个实时环境卷积框架。改变太阳或大气设置必须使正在进行且依赖这些输入的捕获
   按现有快照检查取消，不能发布混合参数的烘焙结果。
8. 关闭/切场景/设备重建均有确定的释放路径；缺少 Compute 或纹理能力不回退 Noop，不自动
   关闭已保存开关。报告当前请求与未生效原因，保留场景可操作性。

## Hex-Tiling 材质

1. 复用 bgfx `49-hextile` 的三角网格与随机旋转，参考 `53-sky2/hextile.sh` 的公共函数拆分。
   `53` 的地形调用者也用了 `texture2DGrad`；不能因 helper 没有 sampler 就认为它不处理梯度。
   `49` 和 `53` 都不是本引擎完整 Metallic/Roughness PBR 多贴图接入。
2. 仅在当前内置不透明 PBR 开启具名参数；关闭时保留原普通采样及现有校准结果。使用既有
   UV0/`uvTransform` 和 five-sampler 契约，不更换资源导入颜色空间，也不添加屏幕空间坐标纹理。
3. 每像素只算一次网格、三组 UV/旋转和一致混合权重，再取三份 BaseColor、Normal、ORM。
   所有采样传随机偏移之前的显式梯度并按旋转变换，以避免 tile 边缘的错误 mip/各向异性。
4. 法线以 mmikk 固定 MIT 原版的表面梯度方法为依据，正确旋转、混合后转回本引擎 TBN。
   上游的 DirectX 纹理轴约定不能直接套到本引擎现有完整 XYZ 法线约定；用方向性测试贴图确认
   旋转、UV 翻转和负缩放。统一权重是使材质贴图对齐的明确适配，不重新发明噪声或混合算法。
5. ORM 的 R/G/B 对应 occlusion/roughness/metallic，线性混合后继续原 PBR 范围约束；不对 ORM
   用颜色 gamma、法线归一化或为金属度构造另一份随机偏移。Emission/Specular 如有绑定也随
   同一贴图图案采样，Lightmap UV2 保持独立。
6. 通过公共 PBR surface 采样同时覆盖主场景和 Surface/MRT；不支持 alpha-cutout、透明
   水、三平面或 SpecGloss，组件和材质共同组合校验可通过原编辑/脚本/MCP 完整失败返回。
7. 核心三张纹理从 3 次变成 9 次采样；若同时使用 Specular/Emission，可达 15 次。
   使用显式 opt-in，不以关闭法线、降采样或改变 PBR 方程掩盖额外成本。只记录此能力开关的
   CPU/GPU 影响，不扩张成其他模块的性能优化。

## 限制

大气透视、逐表面太阳透过率、动态 IBL、轨道或太空相机不在当前能力范围。Hex-Tiling 仅支持前述不透明 PBR 组合，增加的纹理采样成本由使用方按场景评估。

GPU、设备恢复、捕获和编辑链路按实际改动选取已有用例，执行方式见[测试文档](../testing.md#手动集成与-gpu-验证)。共享 Shader 源码不代表所有后端均已完成画面验收。
