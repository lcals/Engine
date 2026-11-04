# SpecGloss v3 历史截图复测

本页保留 v3 阶段的原始截图指标，当前实现与验证结论见[当前结果](current-results.md)。统一显示变换后的同批数据见[历史像素对照](display-matched-results.md#specgloss-修正后的补充对照)。

2026-11-04，使用本轮新构建的 `build/local-ninja-release/editor_app`、macOS Metal 和范例项目原有测试场景，以原相机、光照和材质参数截图；Blender 参考沿用此前的 5.2 场景和原始 PNG。引擎原生截图及参考均为 1280×720。三次 `capture_frame` 均经 `get_frame_capture` 确认为 complete，本轮单独启动的编辑器由 `close_editor` 关闭，退出码 0。

统计为屏幕编码 RGB 0–255、每个球内部半径 72 像素的 MAE/RMSE 与平均亮度；v2 是上次标准直接光修复后的结果，v3 是本轮反射色工作流修复后的结果。所有原始截图、v2 基线、v3 结果均保留。截图没有重映射、调曝光、重新对齐或调整材质以缩小差异。

这些截图本身既不能证明 v3 更准确，也不能证明差异都是引擎错误：Blender Principled IOR 只匹配灰度正入射 F0，不完整对应 glTF 归档 SpecGloss 的漫反射契约。v3 使用了 `c_diff = diffuse * (1 - max(F0))` 及角度 Fresnel 组合；该公式仍需独立规范和数值回归佐证，不能以“渲染器不同”替实现正确性背书。该阶段有 F0 的球普遍比旧版更暗、对原参考 MAE 更大，原数值保留。

两边的光源数值尚未做辐射度量标定，引擎为 Reinhard + gamma 2.2，Blender 为 Standard/sRGB，光滑度首尾值受引擎 0.045 最小粗糙度影响。引擎纹理经 UASTC，Blender 使用源 PNG；两侧该组镜面 RGB 都按线性读取，未覆盖标准 SpecGloss 的 sRGB RGB + 线性 Alpha 纹理。本次 Blender 是原生球而非引擎网格。贴图组第六球引擎关闭直接高光，Blender 保留 IOR=1.5，高光模型不等价。数字是显示编码截图误差，不是线性辐射误差或标准符合性证明。

各组逐像素差图、完整逐球通道结果见 `build/material-comparison/expanded/results/<组>-standard-v3/`；截图与回执分别位于 `projects/showcase/cache/material-comparison/engine-<组>-standard-v3.png` 和 `build/material-comparison/expanded/specgloss-v3-receipts.json`。

## 常量反射色 / IOR

| 参数 | v2 MAE | v3 MAE | v3 RMSE | 引擎亮度 | Blender 亮度 |
| --- | ---: | ---: | ---: | ---: | ---: |
| IOR 1 / F0 0 | 3.117 | 3.117 | 3.908 | 66.12 | 67.85 |
| IOR 1.2 / F0 0.00826 | 3.146 | 3.277 | 4.114 | 65.96 | 67.91 |
| IOR 1.5 / F0 0.04 | 3.345 | 3.989 | 5.071 | 65.36 | 68.27 |
| IOR 1.8 / F0 0.08163 | 3.644 | 5.009 | 6.413 | 64.56 | 68.75 |
| IOR 2 / F0 0.11111 | 3.863 | 5.762 | 7.376 | 64.00 | 69.07 |
| IOR 2.5 / F0 0.18367 | 4.411 | 7.571 | 9.616 | 62.65 | 69.75 |

## 灰度反射色贴图

| 参数 | v2 MAE | v3 MAE | v3 RMSE | 引擎亮度 | Blender 亮度 |
| --- | ---: | ---: | ---: | ---: | ---: |
| spec RGB 0 | 3.117 | 3.117 | 3.908 | 66.12 | 67.85 |
| spec RGB 64 | 3.152 | 3.312 | 4.161 | 65.93 | 67.93 |
| spec RGB 128 | 3.214 | 3.531 | 4.451 | 65.74 | 68.04 |
| spec RGB 191 | 3.273 | 3.748 | 4.748 | 65.55 | 68.15 |
| spec RGB 255 | 3.339 | 3.985 | 5.068 | 65.36 | 68.27 |
| spec RGB 255，关闭直接镜面 | 4.566 | 5.331 | 7.468 | 63.90 | 68.26 |

## 光滑度及来源

| 参数 | v2 MAE | v3 MAE | v3 RMSE | 引擎亮度 | Blender 亮度 |
| --- | ---: | ---: | ---: | ---: | ---: |
| smoothness 0 | 3.646 | 4.465 | 5.511 | 64.74 | 68.38 |
| smoothness 0.5 | 3.348 | 3.995 | 5.079 | 65.36 | 68.27 |
| smoothness 1 | 2.852 | 3.583 | 4.765 | 63.96 | 66.34 |
| spec alpha 64 × smoothness 1 | 3.508 | 4.218 | 5.248 | 65.19 | 68.47 |
| spec alpha 191 × smoothness 1 | 3.261 | 3.934 | 5.790 | 64.90 | 67.71 |
| base alpha 128（smoothnessSource=1） | 3.336 | 3.981 | 5.062 | 65.36 | 68.26 |
