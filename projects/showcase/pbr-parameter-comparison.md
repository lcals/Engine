# 标准 PBR 参数对照：逐项数据

2026-11-04，macOS；本页保留初版参数矩阵及后续阶段记录。当前修复、已验证范围和限制统一见[当前结果](tools/pbr-comparison/current-results.md)，本页旧截图不能代表当前版本已通过全部 PBR 对照。

## 测试方法

- 原生 1280×720；Release Metal 正式场景、预制体和资源导入管线对比 Blender Cycles 128 samples，黑环境、单方向光。球心依次为 (420,250)、(640,250)、(860,250)、(420,470)、(640,470)、(860,470)。
- 每球取中心半径 72 像素内部，逐通道比较显示编码 RGB；MAE、RMSE 和平均亮度以 0–255 表示，亮度是显示编码数值而不是物理亮度。峰值接近 255 时存在裁剪。参数扫描以外默认线性灰 0.5、metallic 0、roughness 0.5。
- 引擎受光颜色经过 Reinhard 和 gamma 2.2；Blender 为 Standard/sRGB。灯光强度数值未完成辐射单位校准。两边屏幕空间误差不能当作 BRDF 误差或测试通过率。
- 两边贴图来自相同 PNG：基础色和发光纹理按 sRGB，法线及 ORM 按线性读取。引擎正式导入经过 UASTC 压缩并转为 RGBA8，Blender 读原 PNG，纹理取样本身可能有压缩差异；纹理均为常量色，UV 组例外。
- Blender 原生球和引擎球的切线不一致；已用引擎同源网格增加法线对照。Blender 原生 Normal Map Strength 与 glTF `normalScale` 公式不同，因此分别保留原生参考和按 glTF XY 缩放、重新归一化的参考；诊断与限制见 [法线专题](tools/pbr-comparison/normal-findings.md)。

下列原始截图和数字为修复前的初版基线；修复后的独立复测另行记录，不能把两批数据混为一次测试。

## 标准语义修正与 GPU 回归

依据 [glTF 2.0 材质规范](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#materials)，ORM 的遮蔽通道只作用于间接照明；法线缩放作用于切线空间 XY，随后归一化。引擎已修正遮蔽错误压暗直射光的问题，并修正正交相机的视线方向。未通过调整曝光或材质参数来降低对照误差。

macOS Metal Release 的真实窗口回归中，方向光单独照明时 AO 0/1 的中心像素红通道为 160/160；关闭直接光、只用环境光时为 0/163。正交相机沿视线移动前后，画面中 194626 个可见像素，颜色差异超过 2 级的像素为 0。回归覆盖具体测试条件，不代表所有材质、光照或图形后端都已验证。

水材质不再对透射、直接光和发光统一施加 AO；其平面反射与环境反射目前共同受材质 AO 遮蔽，可能与平面反射已有的几何可见性重复，属于近似。正交镜像视图的高光和水体厚度尚无独立数值回归。

以原资源和 1280×720 分辨率重拍内置 PBR 场景：[AO 修复中间版](cache/material-comparison/engine-ao-fixed.png)、[法线修复中间版](cache/material-comparison/engine-normal-orm-fixed.png)。这两张仅包含 AO 与正交相机修复，早于后续直接光 BRDF 修正。纯直射光 AO 场景的六球平均显示编码亮度修复前为 0.000、36.566、49.501、58.731、66.235、66.177，中间版均为 66.358/255。这里比较的是截图亮度，GPU 回归另行验证了间接照明仍受 AO 影响。

同源网格法线组中间版对 Blender glTF XY 缩放参考的显示编码 RGB MAE：`normalScale=0.5` 从 5.867 变为 5.886，`normalScale=1` 从 9.147 变为 8.922；其余变量仍未对齐，不能将误差变化归因于单一改动或视为完整 PBR 一致。逐球数据见 `build/material-comparison/expanded/results/normal-orm-gltf-fixed/`。这两个场景引用内置 PBR，不证明范例自定义 Shader 已重新导入或完成实窗验证。

后续金属粗糙度直接光进一步按 glTF 修正 Schlick Fresnel、独立介质与金属 BRDF 混合、相关 Smith GGX 可见性，以及低粗糙度 GGX 分布中错误削峰的分母下限。该阶段 Metal Release GPU 回归经 8 位截图逆显示变换估算线性值：金属度 0.5 实测 0.7756、参考 0.7843；80° 情况实测 5.0801、参考 5.1970；粗糙度 0.045 的峰值实测 0.0773、参考 0.0776。这不是线性浮点缓冲读回，采样与量化限制及容差以测试代码为准。独立 SpecGloss 工作流此后也已按 Khronos 归档扩展及 glTF BRDF 调整；色彩管理差异仍未完成标准化验证。

[standard-v2 阶段 12 组、72 个球的逐项复测](tools/pbr-comparison/standard-v2-results.md)记录了当时的新旧 MAE、RMSE、截图和各项限制；该阶段晚于上面的中间版，早于后续 SpecGloss、IBL 与法线终止线修复。截图指标并非物理正确性的证明。

SpecGloss 本轮再次调整后，三组共 18 球的截图和数据见 [SpecGloss v3 对照](tools/pbr-comparison/specgloss-v3-results.md)。非零 F0 部分的球比旧 Blender 参考更暗，例如 F0 约 0.184 的显示编码 MAE 从 4.411 上升到 7.571；Blender Principled IOR 与 SpecGloss 的漫反射输入并不完全等价，颜色映射、光照单位和纹理解码也未对齐，因此这些截图不能单独证明公式错误或完全正确。

IBL 此后已引入 [Khronos Sample Renderer](https://github.com/KhronosGroup/glTF-Sample-Renderer/blob/main/source/Renderer/shaders/ibl.glsl) 的介质、金属与多次散射能量组合。正式渲染路径的合成白炉测试覆盖三种视角、三种金属度和三种粗糙度，经 8 位截图逆显示转换后 27 组均为约 0.998（白炉参考 1）；有色半金属 RGB 实测 0.8297、0.3975、0.3277，参考 0.8351、0.3964、0.3297。正式 GPU 烘焙还覆盖线性白色、方向变化及高对比程序生成 HDR；高亮小光源仍有误差，摄影 HDR 和线性浮点画面读回尚未验证，详情见 [测试文档](../../docs/testing.md)。SSR 只替换可还原的线性 split-sum 分量，余项沿用环境估计；无 LUT 的环境光/烘焙漫反射使用与 SSR 相同的 DFG 近似进行能量分配，但无法由漫反射数据生成镜面环境。上方的 standard v2 截图仍是 IBL 和 SpecGloss 修正之前的基线，不能代表新效果。

对已有 Blender 线性 EXR 另行应用与引擎相同的 Reinhard 和 gamma 显示变换后，11 组共 66 球的比较见[统一显示变换对照](tools/pbr-comparison/display-matched-results.md)。这是重用已有渲染、统一输出变换的 8 位截图比较，不是线性浮点缓冲比对。

每组截图位于 `projects/showcase/cache/material-comparison/engine-<组名>.png` 与 `build/material-comparison/expanded/blender-<组名>.png`；同源网格和 UV 原始方向图以相应后缀区分。逐球 JSON 与绝对差图位于 `build/material-comparison/expanded/results/<组名>/`。

## 截图索引

| 测试组 | 画面 |
| --- | --- |
| 基础色 | [引擎](cache/material-comparison/engine-basecolor.png) · [Blender](../../build/material-comparison/expanded/blender-basecolor.png) |
| 金属度 | [引擎](cache/material-comparison/engine-metallic.png) · [Blender](../../build/material-comparison/expanded/blender-metallic.png) |
| 粗糙度 | [引擎](cache/material-comparison/engine-roughness.png) · [Blender](../../build/material-comparison/expanded/blender-roughness.png) |
| 法线与 ORM（同源网格） | [引擎](cache/material-comparison/engine-normal-orm.png) · [Blender](../../build/material-comparison/expanded/blender-normal-orm-same-mesh.png) |
| 发光 | [引擎](cache/material-comparison/engine-emission.png) · [Blender](../../build/material-comparison/expanded/blender-emission.png) |
| UV 校正 | [引擎](cache/material-comparison/engine-uv.png) · [Blender](../../build/material-comparison/expanded/blender-uv.png) |
| Alpha 常量 | [引擎](cache/material-comparison/engine-alpha.png) · [Blender](../../build/material-comparison/expanded/blender-alpha.png) |
| Specular / IOR | [引擎](cache/material-comparison/engine-specular.png) · [Blender](../../build/material-comparison/expanded/blender-specular-ior.png) |
| 镜面纹理 | [引擎](cache/material-comparison/engine-specular-map.png) · [Blender](../../build/material-comparison/expanded/blender-specular-map.png) |
| 平滑度来源 | [引擎](cache/material-comparison/engine-smoothness-source.png) · [Blender](../../build/material-comparison/expanded/blender-smoothness-source.png) |
| Alpha 纹理 | [引擎](cache/material-comparison/engine-alpha-map.png) · [Blender](../../build/material-comparison/expanded/blender-alpha-map.png) |

[AO 引擎单侧截图](cache/material-comparison/engine-ao.png)；[UV 原始方向 Blender 截图](../../build/material-comparison/expanded/blender-uv-raw-orientation.png)。

## 基础色与 sRGB 贴图

| 测试 | MAE | RMSE | 引擎平均亮度 | Blender 平均亮度 |
| --- | ---: | ---: | ---: | ---: |
| 灰 0.18 | 3.37 | 4.22 | 44.5 | 42.3 |
| 灰 0.5 | 3.32 | 4.39 | 66.4 | 68.3 |
| 灰 0.8 | 5.96 | 7.70 | 79.7 | 84.9 |
| 红 (0.8,0.05,0.05) | 5.52 | 6.72 | 38.9 | 36.7 |
| 绿 (0.05,0.8,0.05) | 4.95 | 6.10 | 64.8 | 67.4 |
| sRGB 纹理 RGB(128,64,32) | 4.31 | 5.29 | 31.4 | 28.0 |

## 金属度与 ORM 蓝通道

| 测试 | MAE | RMSE | 引擎平均亮度 | Blender 平均亮度 |
| --- | ---: | ---: | ---: | ---: |
| metallic 0 | 3.25 | 4.52 | 66.5 | 68.3 |
| metallic 0.25 | 5.28 | 7.67 | 60.6 | 65.1 |
| metallic 0.5 | 7.37 | 12.24 | 54.6 | 60.9 |
| metallic 0.75 | 8.46 | 15.20 | 49.1 | 55.6 |
| metallic 1 | 6.80 | 11.98 | 43.9 | 48.1 |
| ORM B 255 | 8.68 | 15.71 | 42.8 | 48.1 |

## 粗糙度扫描

| 测试 | MAE | RMSE | 引擎平均亮度 | Blender 平均亮度 |
| --- | ---: | ---: | ---: | ---: |
| roughness 0 | 2.85 | 3.56 | 65.0 | 66.3 |
| roughness 0.045 | 2.94 | 4.69 | 65.0 | 66.4 |
| roughness 0.2 | 3.89 | 10.31 | 65.7 | 67.4 |
| roughness 0.5 | 3.41 | 4.63 | 66.3 | 68.3 |
| roughness 0.8 | 3.58 | 4.47 | 66.1 | 68.5 |
| roughness 1 | 3.66 | 4.55 | 65.8 | 68.4 |

## 法线及 ORM 绿通道（Blender 原生球）

| 测试 | MAE | RMSE | 引擎平均亮度 | Blender 平均亮度 |
| --- | ---: | ---: | ---: | ---: |
| 平法线 | 3.25 | 4.52 | 66.5 | 68.3 |
| 法线强度 0 | 3.35 | 4.44 | 66.5 | 68.3 |
| 法线强度 0.5 | 16.32 | 21.83 | 72.1 | 60.1 |
| 法线强度 1 | 29.54 | 37.30 | 74.8 | 49.3 |
| ORM G 64 | 3.39 | 6.24 | 65.9 | 67.7 |
| ORM G 191 | 3.59 | 4.48 | 66.1 | 68.5 |

## 法线及 ORM 绿通道（相同源网格）

| 测试 | MAE | RMSE | 引擎平均亮度 | Blender 平均亮度 |
| --- | ---: | ---: | ---: | ---: |
| 平法线 | 3.27 | 4.54 | 66.5 | 68.2 |
| 法线强度 0 | 3.37 | 4.46 | 66.5 | 68.3 |
| 法线强度 0.5 | 5.87 | 9.80 | 72.1 | 71.4 |
| 法线强度 1 | 9.15 | 15.71 | 74.8 | 70.3 |
| ORM G 64 | 3.41 | 6.28 | 65.9 | 67.7 |
| ORM G 191 | 3.61 | 4.50 | 66.1 | 68.4 |

## 发光颜色及 sRGB 发光贴图

| 测试 | MAE | RMSE | 引擎平均亮度 | Blender 平均亮度 |
| --- | ---: | ---: | ---: | ---: |
| emission 0 | 6.63 | 8.00 | 15.9 | 10.5 |
| emission 灰 0.25 | 14.58 | 14.62 | 123.6 | 138.2 |
| emission 灰 0.5 | 33.08 | 33.09 | 155.3 | 188.3 |
| emission 灰 1 | 68.83 | 68.83 | 186.1 | 255.0 |
| emission 红 1 | 25.95 | 39.96 | 51.1 | 62.5 |
| sRGB 发光纹理 RGB(128,64,32) | 6.00 | 7.82 | 75.4 | 77.9 |

## UV 原始坐标约定

| 测试 | MAE | RMSE | 引擎平均亮度 | Blender 平均亮度 |
| --- | ---: | ---: | ---: | ---: |
| scale(1,1) offset(0,0) | 42.65 | 54.64 | 33.3 | 28.4 |
| scale(2,2) offset(0,0) | 31.97 | 45.68 | 48.1 | 46.2 |
| scale(1,1) offset(.25,0) | 31.95 | 46.42 | 47.3 | 47.0 |
| scale(1,1) offset(0,.25) | 44.66 | 55.53 | 31.2 | 30.3 |
| scale(.5,.5) offset(0,0) | 42.52 | 55.54 | 33.2 | 27.5 |
| scale(1,1) offset(.5,.5) | 23.30 | 37.93 | 61.6 | 58.8 |

## UV 纹理原点对齐

| 测试 | MAE | RMSE | 引擎平均亮度 | Blender 平均亮度 |
| --- | ---: | ---: | ---: | ---: |
| scale(1,1) offset(0,0) | 6.19 | 7.55 | 33.3 | 29.5 |
| scale(2,2) offset(0,0) | 5.52 | 6.62 | 48.1 | 46.9 |
| scale(1,1) offset(.25,0) | 5.31 | 6.67 | 47.3 | 45.9 |
| scale(1,1) offset(0,.25) | 5.55 | 6.77 | 31.2 | 27.6 |
| scale(.5,.5) offset(0,0) | 5.13 | 6.10 | 33.2 | 30.4 |
| scale(1,1) offset(.5,.5) | 5.38 | 6.55 | 61.6 | 63.1 |

## Alpha 截断

| 测试 | MAE | RMSE | 引擎平均亮度 | Blender 平均亮度 |
| --- | ---: | ---: | ---: | ---: |
| alpha .49 cutoff .5 | 0.08 | 0.28 | 0.0 | 0.1 |
| alpha .5 cutoff .5 | 3.32 | 4.39 | 66.4 | 68.3 |
| alpha .51 cutoff .5 | 3.38 | 4.58 | 66.3 | 68.3 |
| alpha 0 cutoff 0 | 3.41 | 4.63 | 66.3 | 68.3 |
| alpha .5 cutoff 0 | 3.45 | 4.48 | 66.2 | 68.3 |
| alpha 1 cutoff 0 | 3.51 | 4.64 | 66.2 | 68.3 |

## Specular 工作流与 IOR

以 IOR 正入射关系 F0=((IOR−1)/(IOR+1))² 设置引擎 specularColor，smoothness=0.5；Blender 调整 Principled IOR、roughness=0.5。两者只匹配正入射 F0，不证明完整 BRDF 等价。

| 测试 | MAE | RMSE | 引擎平均亮度 | Blender 平均亮度 |
| --- | ---: | ---: | ---: | ---: |
| IOR 1 / F0 0 | 3.12 | 3.91 | 66.1 | 67.9 |
| IOR 1.2 / F0 0.00826 | 3.14 | 3.97 | 66.2 | 67.9 |
| IOR 1.5 / F0 0.04 | 3.38 | 4.58 | 66.3 | 68.3 |
| IOR 1.8 / F0 0.08163 | 3.78 | 5.72 | 66.4 | 68.8 |
| IOR 2 / F0 0.11111 | 4.16 | 5.82 | 66.3 | 69.1 |
| IOR 2.5 / F0 0.18367 | 5.11 | 8.30 | 65.9 | 69.7 |

引擎截图为 `engine-specular.png`，Blender 图为 `blender-specular-ior.png`。

## Specular 灰度纹理（Blender IOR 1.5 参考）

前五球按灰度纹理乘 F0，再用正入射关系转换 IOR；末球引擎关闭直接镜面，Blender 保持 IOR 1.5，不能认为是相同开关。

| 测试 | MAE | RMSE | 引擎平均亮度 | Blender 平均亮度 |
| --- | ---: | ---: | ---: | ---: |
| spec RGB 0 | 3.12 | 3.91 | 66.1 | 67.9 |
| spec RGB 64 | 3.14 | 3.98 | 66.2 | 67.9 |
| spec RGB 128 | 3.23 | 4.17 | 66.2 | 68.0 |
| spec RGB 191 | 3.32 | 4.40 | 66.3 | 68.1 |
| spec RGB 255 | 3.45 | 4.48 | 66.2 | 68.3 |
| spec RGB 255，关闭直接镜面 | 4.57 | 6.59 | 65.0 | 68.3 |

## Specular 灰度纹理（末球 IOR 1 近似）

仅末球 Blender 改为 IOR 1；它也改变 Fresnel/能量分配，因此不严格等价。

| 测试 | MAE | RMSE | 引擎平均亮度 | Blender 平均亮度 |
| --- | ---: | ---: | ---: | ---: |
| spec RGB 0 | 3.12 | 3.91 | 66.1 | 67.9 |
| spec RGB 64 | 3.14 | 3.98 | 66.2 | 67.9 |
| spec RGB 128 | 3.23 | 4.17 | 66.2 | 68.0 |
| spec RGB 191 | 3.32 | 4.40 | 66.3 | 68.1 |
| spec RGB 255 | 3.45 | 4.48 | 66.2 | 68.3 |
| spec RGB 255，关闭直接镜面 | 3.87 | 4.81 | 65.0 | 67.9 |

## Smoothness 标量及贴图 Alpha 来源

前三球测试 smoothness 0、0.5、1；四五球读取 specular 纹理 alpha；末球从 baseColor 纹理 alpha 读取。Blender 以 roughness = 1 − smoothness 对照。

| 测试 | MAE | RMSE | 引擎平均亮度 | Blender 平均亮度 |
| --- | ---: | ---: | ---: | ---: |
| smoothness 0 | 3.61 | 4.49 | 65.9 | 68.4 |
| smoothness 0.5 | 3.32 | 4.39 | 66.4 | 68.3 |
| smoothness 1 | 2.85 | 3.56 | 65.0 | 66.3 |
| spec alpha 64 × smoothness 1 | 3.53 | 4.43 | 66.2 | 68.5 |
| spec alpha 191 × smoothness 1 | 3.39 | 6.24 | 65.9 | 67.7 |
| base alpha 128（smoothnessSource=1） | 3.50 | 4.64 | 66.2 | 68.3 |

## BaseColor 纹理 Alpha 与截断阈值

每球均设置 cutoff 0.5；两个截图中 alpha 字节 64/127 裁切，128/129/191/255 显示。纹理经过引擎 UASTC 编码。

| 测试 | MAE | RMSE | 引擎平均亮度 | Blender 平均亮度 |
| --- | ---: | ---: | ---: | ---: |
| alpha 64 cutoff .5 | 0.08 | 0.28 | 0.0 | 0.1 |
| alpha 127 cutoff .5 | 0.08 | 0.29 | 0.0 | 0.1 |
| alpha 128 cutoff .5 | 3.38 | 4.58 | 66.3 | 68.3 |
| alpha 129 cutoff .5 | 3.41 | 4.63 | 66.3 | 68.3 |
| alpha 191 cutoff .5 | 3.45 | 4.48 | 66.2 | 68.3 |
| alpha 255 cutoff .5 | 3.51 | 4.64 | 66.2 | 68.3 |

## ORM R 遮蔽：引擎单侧观察

Blender Principled 没有直接等价的 AO 输入，此处没有 Blender 像素对照；仅记录修复前引擎对不同 R 通道值的响应。当时引擎错误地将 ORM 遮蔽乘到包括直射光在内的受光结果；该问题已按 glTF 语义修复，以下数字保留作为修复前基线。

| ORM R 字节 | 引擎平均亮度 |
| --- | ---: |
| AO R 0 | 0.0 |
| AO R 64 | 36.6 |
| AO R 128 | 49.5 |
| AO R 191 | 58.7 |
| AO R 255 | 66.2 |
| AO R 255（重复） | 66.2 |

## 初版观察和当时未覆盖项

以下描述对应本页历史截图；后续变化见[当前结果](tools/pbr-comparison/current-results.md)。

- 正交投影的平行视线已修复，并在具体窗口用例中验证；镜像视图和水体仍需独立验证。
- 原生球法线强度 1 的 MAE 为 29.54；换成同源网格后为 9.1 左右。源网格/切线会显著影响结果，但剩余误差还混有切线生成、强度公式、色调映射及导入压缩，不能据此锁定唯一原因。
- UV 原始方向图保留；对齐图按 u′ = u×scaleU+offsetU、v′ = v×scaleV+offsetV，Blender 采样使用 (u′, 1−v′) 匹配纹理原点约定。不能把约定不同直接判为引擎缺陷。
- Alpha 测试覆盖 opaque 管线下 alphaCutoff 严格小于边界，未验证半透明混合、折射与排序。
- 还未完成与 Blender 同条件的 AO、真实复杂贴图、切线空间标准一致性、IBL、有色镜面反射、严格等价的 specularHighlights、fogInfluence、双面及颜色管理的线性缓冲逐项对照；不可宣称全部参数通过。
- Blender 已提供每组线性 EXR；引擎尚未导出对应渲染阶段的线性浮点图，不能执行严格的线性能量误差比较。

## 初版内置 PBR 参数覆盖清单

参数以 `src/engine/runtime/resource/builtin_resources.cpp` 和 `pbr_surface.sh` 为准。下列“已测”指已采图并测得像素，不表示数值误差已达成验收标准。

| 引擎参数 | 本页历史覆盖 |
| --- | --- |
| `baseColor` | 已测 RGB 灰度及彩色；alpha 常量在 `alphaCutoff` 组测试。 |
| `baseColorTexture` | 已测 sRGB 常量颜色和 UV 四象限；alpha 纹理已测六档阈值。 |
| `metallic` | 已扫描 0、0.25、0.5、0.75、1。 |
| `roughness` | 已扫描 0、0.045、0.2、0.5、0.8、1；引擎实际下限 0.045。 |
| `normalTexture` / `normalScale` | 已测平法线和倾斜法线 0、0.5、1，并增加同源网格与 glTF XY 缩放参考；切线空间及颜色管理等差异仍需进一步隔离。 |
| `ormTexture` | G 粗糙度和 B 金属度已有双侧对照；R 遮蔽仅有引擎单侧结果。常量图不能验证复杂纹理细节。 |
| `emissionColor` / `emissionTexture` | 已测灰度、红色和 sRGB 常量发光纹理。 |
| `uvTransform` | 已测缩放与偏移；保留原始方向及 V 原点对齐两套结果。 |
| `alphaCutoff` | 已测不透明管线的常量 alpha 边界；alpha 纹理已测六档，混合和排序尚未测。 |
| `specularWorkflow` / `specularColor` / `smoothness` | 已通过六组 IOR 和 F0 做近似对照；BRDF 模型并不完全等价。 |
| `specularTexture` / `smoothnessSource` / `specularHighlights` | 已测灰度镜面纹理、纹理 alpha 与亮度来源；关闭直接高光仅有 Blender IOR 1 和 1.5 两种非等价参考，有色镜面贴图尚未测。 |
| `fogInfluence` | 尚未测试雾影响。 |
| `lightingMode` | 引擎内部选择 PBR、Unlit 或 Blinn–Phong，本轮仅测 PBR。 |

透明混合、IBL、场景光照与色调映射属于材质或渲染配套能力，也必须单独验证；它们不应被误写成上述参数已经通过的证据。

脚本：`projects/showcase/tests/pbr_compare_pixels.py`。同图自检 6 个球的 MAE、RMSE 均为 0。场景启动与截图方式见 `projects/showcase/pbr-comparison.md`。
