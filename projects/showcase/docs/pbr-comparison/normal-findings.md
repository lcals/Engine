# 法线贴图标准对照与切线诊断

本文保留法线问题的**历史诊断过程**，各表格只代表对应阶段的实现；当前实现、最新实测和验收边界统一见 [当前对照结果](current-results.md)。不能将旧图、隔离实验或某一阶段的“修复后”数值当作最新结果。

最初以同一 `.mesh` 构造 Blender 参考，使用相同法线 PNG；引擎端纹理经过 UASTC 编码，Blender 端使用 PNG，尚非完全相同的采样数据。两侧是 1280×720 原生截图，指标按既有脚本计算六个球内部的显示编码 RGB，范围 0–255。

Blender 原生 `Normal Map Strength` 和 glTF 2.0 `normalTexture.scale` 是不同定义。增加一组独立参考，在 Blender 里按 glTF 规则缩放法线贴图解码后的 XY，再以单位强度的 Normal Map 节点处理；原始 Blender Strength 参考保留。两组都使用 Blender 的切线空间处理，未修改引擎参数来迎合结果。

| 测试 | Blender 原生节点 MAE | glTF XY 同契约参考 MAE |
| --- | ---: | ---: |
| 无法线贴图 | 3.274 | 3.274 |
| 缩放 0 | 3.374 | 3.374 |
| 缩放 0.5 | 5.871 | 5.867 |
| 缩放 1 | 9.147 | 9.147 |

表格记录的是修复前的原始截图；数值不能作为修正后的效果结论。对该材质球，0.5 的语义修正影响较小，不能把大部分剩余误差归因于 Strength 定义。

在 AO 和正交相机修正后重新构建并启动 Release 编辑器，通过 MCP 预览同一场景、使用原材质及镜头参数输出中间版 `engine-normal-orm-fixed.png`，实际帧缓冲仍为 1280×720，与 glTF 同契约 Blender 参考逐球比较。随后直接光 GGX/Fresnel/Smith 又有修正，该阶段结果见 [标准直接光修复后复测](standard-v2-results.md)：

| 测试 | 修复前 MAE | 修复后 MAE |
| --- | ---: | ---: |
| 无法线贴图 | 3.274 | 3.358 |
| 缩放 0 | 3.374 | 3.404 |
| 缩放 0.5 | 5.867 | 5.886 |
| 缩放 1 | 9.147 | 8.922 |
| ORM G 64 | 3.413 | 3.282 |
| ORM G 191 | 3.609 | 3.527 |

显示转换和灯光单位尚未对齐；引擎纹理经 UASTC 而 Blender 使用源 PNG。以上数据只能报告当前场景的差异，不能证明物理 BRDF 已经完全匹配。使用原有 AO 场景另存的 `engine-ao-fixed.png` 中，AO 0、64、128、191、255 各球在仅直射光、无环境光条件下的平均显示亮度均为 66.358/255；旧图对应值约为 0、36.566、49.501、58.731、66.235，验证该条件下 AO 不再遮蔽直射光。两组材质使用引擎内置 PBR shader，未验证自定义 shader。

`compare_tangent_frames.py` 在相同位置、法线、索引和 UV 的网格上，比较 Blender `mesh.calc_tangents` 计算的 MikkTSpace 和独立复现的引擎当前切线 fallback：12288 个三角面角中，平均角度差约 0.165°、95 分位约 0.93°，1 个角大于 1°，2 个角的手性不一致。该分析是 CPU 算法复现，不是引擎 GPU buffer 回读；退化位置、网格拓扑和浮点精度可能影响结果。该球上的结果不能证明其他网格或镜像 UV 与 MikkTSpace 等价。

可复现命令和输出位置见 [工具说明](tools.md)。截图、线性 EXR 与机器可读结果位于 `build/material-comparison/expanded/`。要确认剩余差异，应先对齐相机视线、光照单位和显示转换，再对照相同解码纹理的线性结果，并增加具有 UV 接缝、镜像和不均匀缩放的网格；不能依据当前显示截图直接调整法线系数。

后续将 Blender 的线性 EXR 按引擎相同的 Reinhard/gamma 显示变换输出后，同源网格的法线强度 0、0.5、1 的显示编码 MAE 分别为 0.164、2.855、6.705，见[统一显示变换对照](display-matched-results.md)。这一组使用 4×4 常量法线图，V 坐标翻转不会改变采样颜色。对范例正式导入缓存中的 BasisU 数据使用对应版本转码器离线解码，16 个像素均为源图 RGB `(191,128,238)`，因此该案例的 CPU 纹理转码未观察到数值差异；该检查不是 GPU 纹理采样回读，也不能证明 GPU 切线或插值与 Blender 一致。光照单位及渲染阶段线性输出仍需对齐，不根据此截图改动法线强度。

## 世界法线与方向光的隔离诊断

为区分法线框架和光照计算，在已有同源球体参考中仅保留 `normalScale=1` 的球，将 Blender Cycles Normal Map 节点输出的世界空间法线转换到引擎坐标并编码为线性发光值，输出 32 位 EXR；无 BRDF、灯光或显示变换参与。独立 CPU 代码根据引擎网格、fallback 切线、正交投影和片元切线框架计算法线，在球内部避开轮廓的 373 个像素进行比较。输入的常量法线贴图使用 `(191,128,238)`，与此前对正式导入缓存的 CPU 转码结果一致。

| 指标 | 结果 |
| --- | ---: |
| 法线夹角平均值 | 0.01976° |
| 法线夹角 P95 | 0.03773° |
| 法线夹角最大值 | 0.04740° |
| 方向光夹角 | 0.000005° |
| `max(N·L, 0)` 绝对误差平均值 | 0.000126 |
| `max(N·L, 0)` 绝对误差 P95 | 0.000313 |
| `max(N·L, 0)` 绝对误差最大值 | 0.000579 |

引擎光源方向根据场景旋转和 `WorldSystem` / 灯光打包公式计算；Blender 方向从实际光源世界矩阵取得并转换至引擎坐标。结果是此测试条件下的参考法线与方向点积，不包含灯光强度或 BRDF，支持继续调查后续光照及实际 GPU 管线，不能据此修改法线系数。

复现命令（先按工具说明生成 `normal-orm-gltf-same-mesh` 参考）：

```sh
/Applications/Blender.app/Contents/MacOS/Blender -b -t 8 --python-exit-code 1 \
  --python projects/showcase/tools/pbr-comparison/diagnose_normal_field.py
```

产物保存在忽略目录 `build/material-comparison/normal-field/`，包括法线 EXR 和逐点 JSON。**引擎侧为 CPU 算法复现，并非 GPU 缓冲读取**；纹理实际 GPU 采样、切线上传及插值、灯光 GPU 数据仍未被该实验验证。光照响应依然需要同条件实测，本实验不证明其他网格、镜像 UV 或非均匀缩放一致。

## GPU 法线与背光区域诊断（2026-11-04）

在 `build/material-comparison/gpu-normal-field/` 创建隔离的公开临时项目，复用同一球体网格、常量法线图、原相机和材质。通过正式项目 Shader 分别输出法线贴图后的世界空间着色法线，以及未经法线贴图扰动、插值后的**平滑基础法线**（不是逐三角面的几何法线）。项目 `pbr_surface.sh` 与当前引擎内置同名文件逐字节相同。诊断 Shader 只输出法线用于观察，不修改正式 PBR 结果。编辑器通过 MCP 正常退出，退出码 0。

GPU 截图为 2560×1440 的 8 位 SDR；对 Reinhard／gamma 2.2 做逆变换并将 `[0,1]` 映射回 `[-1,1]`，再与 1280×720 的 Blender 法线 EXR 在相对坐标对应。受截图量化、不同采样位置和抗锯齿影响，这**不是线性浮点 GPU 回读**。球体内部每隔 2 像素取样，共 3852 点，着色法线与 Blender 参考夹角平均 0.575°、P95 1.032°、最大 1.586°。

进一步依据 GPU 截图估计的方向点积筛选，并读取之前未修改的 `standard-v2` 引擎截图和统一显示变换 Blender 参考。亮度是 8 位显示编码 RGB 的均值，归一化到 0–1：

| 条件 | 样点 | 引擎平均亮度 | Blender 平均亮度 |
| --- | ---: | ---: | ---: |
| 着色法线 `N·L > 0.02`，基础法线 `N·L < -0.02` | 312 | 0.16068 | 0.000038 |
| 着色法线 `N·L > 0.02`，基础法线 `N·L > 0.02` | 3365 | 0.32099 | 0.30987 |

该测试显示原有截图中的一部分差异集中在法线贴图朝向光源、基础法线却背光的区域，支持继续调查着色法线的地平线可见性；它本身不证明其余差异的成因，也不能将 Blender 的具体修正规则当作 glTF 的唯一要求。当前灯光单位未独立标定；旧截图与新诊断截图分开采集，后者分辨率更高，并仅代表所测球体和方向光。

复现时先运行前文的 Blender 世界法线诊断与已有显示变换步骤，然后：

```sh
python3 projects/showcase/tools/pbr-comparison/diagnose_gpu_normals.py
/Applications/Blender.app/Contents/MacOS/Blender -b -t 2 --python-exit-code 1 \
  --python projects/showcase/tools/pbr-comparison/compare_gpu_normals.py -- <上一步输出的 work 目录>
```

截图、MCP 回执与逐点数据留在忽略目录；原始截图和参考没有被覆盖。

## 直接光背光保护后的同场景复拍（2026-11-04）

在 macOS Metal Release 编辑器中重新打开原有 `scenes/pbr-test-normal-orm.scene`，保留原相机、光照和材质；实际截图为 **1280×720**，未缩放。以已有 `standard-v2` 引擎原截图和已有 Blender Cycles 同源网格、glTF 法线缩放参考比较；Blender 参考已按前述 Reinhard／gamma 2.2 统一显示变换。每球仍取中心半径 72 像素；数值是显示编码 RGB 的平均绝对误差（MAE，0–255）：

| 球及参数 | 修复前 MAE | 修复后 MAE | 新旧引擎截图差异 MAE |
| --- | ---: | ---: | ---: |
| 平法线 | 0.276 | 0.276 | 0.000 |
| 法线强度 0 | 0.164 | 0.164 | 0.000 |
| 法线强度 0.5 | 2.855 | 1.200 | 1.733 |
| 法线强度 1 | 6.705 | 2.780 | 4.004 |
| ORM G 64 | 0.307 | 0.307 | 0.000 |
| ORM G 191 | 0.551 | 0.551 | 0.000 |

沿用上一节 312 个“着色法线朝光且平滑基础法线背光”样点，修复前与参考的 MAE 为 40.965、修复后为 0.010；3365 个两者都朝光的样点，修复前后 MAE 均为 2.847。该分类来自量化截图反推法线，且诊断图和原场景截图分开采集；这支持所测背光区域的问题得到修正，不代表其他材质、光源和场景均已对齐。强度 1 仍有 2.780 的全球 MAE；本测试不决定剩余误差的成因，也不是线性 HDR 辐亮度比较。灯光单位尚未独立标定，引擎纹理经过 UASTC、Blender 使用源 PNG，采样及 BRDF 等差异仍在。

复拍命令与逐球比较命令：

```sh
python3 projects/showcase/tools/pbr-comparison/capture_normal_horizon.py
/Applications/Blender.app/Contents/MacOS/Blender -b -t 2 --python-exit-code 1 \
  --python projects/showcase/tests/pbr_compare_pixels.py -- \
  <复拍截图> build/material-comparison/display-matched/blender-normal-orm-gltf-same-mesh.png \
  <输出目录> build/material-comparison/expanded/normal-orm-labels.json
```

本次 MCP 回执确认截图 `complete`，编辑器通过 `close_editor` 退出码 0；截图、逐球 JSON、差图、逐项回执与编辑器日志保存在忽略目录 `build/material-comparison/horizon-recapture/1793764298445513000/` 和 `projects/showcase/cache/material-comparison/`。原场景和旧图未覆盖。本次仅复拍这一组场景，不能据此声称全部 PBR 参数与 Blender 一致。

## 剩余误差分区及 Cycles 凹凸校正隔离（2026-11-04）

将上一节复拍图、默认 Blender 参考图与前一节 GPU 法线诊断的 3852 个样点关联，按量化截图反推的平滑基础法线方向 `N·L` 分组（区间左闭右开）。误差与有符号差均是显示编码 RGB，0–255；正值表示引擎偏亮：

| 基础法线 `N·L` | 样点 | MAE | 引擎减 Blender 均值 |
| --- | ---: | ---: | ---: |
| -0.20 ～ -0.05 | 237 | 0.000 | 0.000 |
| 0 ～ 0.05 | 114 | 23.877 | 23.333 |
| 0.05 ～ 0.10 | 121 | 13.802 | 13.736 |
| 0.10 ～ 0.20 | 280 | 7.593 | 7.593 |
| 0.20 ～ 0.40 | 659 | 2.974 | 2.974 |
| 0.40 ～ 0.60 | 730 | 1.240 | 1.240 |
| 0.60 ～ 0.80 | 803 | 0.695 | 0.660 |
| 0.80 ～ 1 | 701 | 0.930 | 0.922 |

这组样点位于法线强度 1 的球体内部；正常图与复拍图并非同一帧，且高分辨率法线图通过 8 位截图反推，接近 0 的分组会受量化及采样位置影响。结果表明误差集中在基础法线接近地平线的朝光区域，而不是证明整个 GPU 数据或 BRDF 全面一致。

直接读取保存的 Blender 5.2 参考场景，六个材质的 `cycles.use_bump_map_correction` 均为 `true`；球体 `shadow_terminator_offset` 为 0、`shadow_terminator_geometry_offset` 为 0.1，并关闭了其他球的阴影可见性。以同一个保存场景和 128 个采样，在内存中**仅关闭**六个材质的凹凸校正，独立重渲染 1280×720 EXR，再做同样 Reinhard/gamma 显示变换后，与当前引擎截图对照：

| 参数 | 原默认参考 MAE | 仅关闭凹凸校正的参考 MAE |
| --- | ---: | ---: |
| 平法线 | 0.276 | 0.276 |
| 法线强度 0 | 0.164 | 0.164 |
| 法线强度 0.5 | 1.200 | 0.386 |
| 法线强度 1 | 2.780 | 0.734 |
| ORM G 64 | 0.307 | 0.307 |
| ORM G 191 | 0.551 | 0.551 |

这项隔离实验说明该校正是**当前场景**的主要差异来源；关闭它仅用于定位，并不替换正式 Blender 默认参考，也不能以此宣称标准 PBR 已对齐。截图依然是 8 位，灯光单位、其他法线纹理、点光和环境照明尚需独立验证。

[Blender 5.2 `bsdf.h`](https://github.com/blender/blender/blob/fbe6228777e7d9afefcd61a413844e790ae75db7/intern/cycles/kernel/closure/bsdf.h) 表明，基础法线可见性限制用于相关 closure；凹凸校正启用时，仅漫反射 closure 进一步应用地平线平滑。其注释指向 Conty Estevez、Lecocq 和 Stein 的 [《A Microfacet-Based Shadowing Function to Solve the Bump Terminator Problem》](https://doi.org/10.1007/978-1-4842-4427-2_12)。这是法线贴图地平线附近的遮蔽近似，不是 glTF 强制规定的唯一公式。

**许可结论更正（2026-11-04）：** 之前将 Blender 主程序的 GPL 许可笼统套用于上述 Cycles 文件，并据出版社论文的 CC BY-NC-ND 文本许可推断缺少可复用实现，这两处判断均不准确。该 `bsdf.h` 文件自身明确标注 `Apache-2.0`；此外，出版方官方 [Ray Tracing Gems 配套代码](https://github.com/Apress/ray-tracing-gems/blob/424bf8d58f9e382e8d71ab21e4c0a5a5684669d2/Ch_12_A_Microfacet-Based_Shadowing_Function_to_Solve_the_Bump_Terminator_Problem/terminator.cpp) 以 MIT 提供同一方法的两个函数，章节文件没有另行许可限制。本轮实现选用这份固定版本的 MIT 配套代码，保留原有 `0.125` 系数，仅为数值稳定性做代数等价改写；来源、完整许可与适用范围见 [第三方声明](../../../../third_party/notices/ray-tracing-gems-terminator.md)。这一更正不改变以上历史实测数值，也不能把关闭 Blender 校正的诊断图替换成正式参考。

复现隔离实验（不保存或覆盖源 `.blend`；前提是已有工具说明所述的参考场景）：

```sh
/Applications/Blender.app/Contents/MacOS/Blender -b \
  build/material-comparison/expanded/blender-normal-orm-gltf-same-mesh.blend \
  -t 8 --python-exit-code 1 --python \
  projects/showcase/tools/pbr-comparison/diagnose_bump_correction.py -- \
  --output-dir build/material-comparison/bump-correction-diagnostic/reproduction
/Applications/Blender.app/Contents/MacOS/Blender -b -t 2 --python-exit-code 1 \
  --python projects/showcase/tools/pbr-comparison/encode_linear_reference.py -- \
  build/material-comparison/bump-correction-diagnostic/reproduction/blender-bump-off.exr \
  build/material-comparison/bump-correction-diagnostic/reproduction/blender-bump-off.png
/Applications/Blender.app/Contents/MacOS/Blender -b -t 2 --python-exit-code 1 \
  --python projects/showcase/tests/pbr_compare_pixels.py -- \
  projects/showcase/cache/material-comparison/engine-normal-orm-horizon-1793764298445513000.png \
  build/material-comparison/bump-correction-diagnostic/reproduction/blender-bump-off.png \
  build/material-comparison/bump-correction-diagnostic/reproduction/comparison \
  build/material-comparison/expanded/normal-orm-labels.json
```

保存的 `settings.json` 显示原始校正与对象设置；EXR 和比较结果位于忽略目录。误差分区可用 `analyze_normal_residual.py` 复现，传入上一节 `compare.json`、复拍图、默认参考图和输出 JSON。
