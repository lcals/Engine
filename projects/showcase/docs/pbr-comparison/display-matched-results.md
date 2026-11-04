# PBR 参考图统一显示变换后的历史像素对照

本页重算历史截图，不代表当前代码重新渲染的结果。法线强度 1 的 6.705 是终止线修复前数据；SpecGloss 表分别来自 v2、v3 阶段。当前验证见[当前结果](current-results.md)，法线定位过程见[法线专题](normal-findings.md)。

2026-11-04，复用已有的 macOS Metal Release 引擎 `standard-v2` 截图，以及 Blender Cycles 的 32 位线性 EXR。没有重渲染或修改材质、灯光；对 Blender EXR 的线性 RGB 应用引擎当前相同的显示变换：曝光 1、Reinhard `x/(1+x)` 和 gamma `1/2.2`，再量化为 PNG。原有 Blender Standard/sRGB 截图及原报告保持不变。

截图均为 1280×720；每个球的统计区域是中心半径 72 像素的圆。表中为显示编码 RGB 的 MAE，单位为 0–255。“原始”是 Blender Standard/sRGB 截图与引擎比较，“统一变换”是上述线性 EXR 转换后的比较。

| 参数 | 原始 | 统一变换 |
| --- | ---: | ---: |
| 基础色灰 0.18 | 2.601 | 0.242 |
| 基础色灰 0.5 | 3.310 | 0.296 |
| 基础色灰 0.8 | 5.898 | 0.331 |
| 基础色红 | 5.081 | 0.267 |
| 基础色绿 | 5.089 | 0.268 |
| sRGB 基础色纹理 | 4.200 | 0.251 |
| 金属度 0 | 3.299 | 0.296 |
| 金属度 0.25 | 3.603 | 0.430 |
| 金属度 0.5 | 4.117 | 0.604 |
| 金属度 0.75 | 4.922 | 0.837 |
| 金属度 1 | 6.064 | 1.202 |
| ORM B 255 | 6.071 | 1.201 |
| 粗糙度 0 | 2.881 | 0.313 |
| 粗糙度 0.045 | 2.892 | 0.287 |
| 粗糙度 0.2 | 3.177 | 0.320 |
| 粗糙度 0.5 | 3.302 | 0.294 |
| 粗糙度 0.8 | 3.506 | 0.655 |
| 粗糙度 1 | 3.642 | 0.945 |
| 发光 0 | 5.365 | 0.327 |
| 发光灰 0.25 | 14.565 | 0.037 |
| 发光灰 0.5 | 33.053 | 0.025 |
| 发光灰 1 | 68.815 | 0.019 |
| 发光红 1 | 26.515 | 0.225 |
| sRGB 发光纹理 | 5.724 | 0.111 |

以下为同一方法的其余七组。法线组使用同源网格与 glTF XY 缩放参考；镜面组仅正入射灰度 F0 对应 Blender IOR，最后一项关闭直接高光没有严格等价参考；Alpha 是裁切，不是透明混合。

| 参数 | 原始 | 统一变换 |
| --- | ---: | ---: |
| 平法线 | 3.318 | 0.276 |
| 法线强度 0 | 3.368 | 0.164 |
| 法线强度 0.5 | 5.846 | 2.855 |
| 法线强度 1 | 8.902 | 6.705 |
| ORM G 64 | 3.275 | 0.307 |
| ORM G 191 | 3.479 | 0.551 |
| UV scale(1,1) offset(0,0) | 5.470 | 0.529 |
| UV scale(2,2) offset(0,0) | 5.469 | 0.570 |
| UV scale(1,1) offset(.25,0) | 5.002 | 0.823 |
| UV scale(1,1) offset(0,.25) | 5.105 | 0.376 |
| UV scale(.5,.5) offset(0,0) | 5.406 | 0.271 |
| UV scale(1,1) offset(.5,.5) | 5.320 | 0.377 |
| alpha .49 cutoff .5 | 0.078 | 0.000 |
| alpha .5 cutoff .5 | 3.310 | 0.296 |
| alpha .51 cutoff .5 | 3.305 | 0.295 |
| alpha 0 cutoff 0 | 3.302 | 0.294 |
| alpha .5 cutoff 0 | 3.299 | 0.295 |
| alpha 1 cutoff 0 | 3.300 | 0.295 |
| 贴图 alpha 64 cutoff .5 | 0.078 | 0.000 |
| 贴图 alpha 127 cutoff .5 | 0.083 | 0.000 |
| 贴图 alpha 128 cutoff .5 | 3.305 | 0.295 |
| 贴图 alpha 129 cutoff .5 | 3.302 | 0.294 |
| 贴图 alpha 191 cutoff .5 | 3.299 | 0.295 |
| 贴图 alpha 255 cutoff .5 | 3.300 | 0.295 |
| IOR 1 / F0 0 | 3.117 | 0.213 |
| IOR 1.2 / F0 0.00826 | 3.146 | 0.237 |
| IOR 1.5 / F0 0.04 | 3.345 | 0.332 |
| IOR 1.8 / F0 0.08163 | 3.644 | 0.450 |
| IOR 2 / F0 0.11111 | 3.863 | 0.562 |
| IOR 2.5 / F0 0.18367 | 4.411 | 0.885 |
| 镜面贴图 RGB 0 | 3.117 | 0.213 |
| 镜面贴图 RGB 64 | 3.152 | 0.245 |
| 镜面贴图 RGB 128 | 3.214 | 0.277 |
| 镜面贴图 RGB 191 | 3.273 | 0.301 |
| 镜面贴图 RGB 255 | 3.339 | 0.332 |
| 镜面贴图 RGB 255，关闭直接镜面 | 4.566 | 1.617 |
| smoothness 0 | 3.646 | 0.946 |
| smoothness 0.5 | 3.348 | 0.333 |
| smoothness 1 | 2.852 | 0.278 |
| 镜面 alpha 64 × smoothness 1 | 3.508 | 0.637 |
| 镜面 alpha 191 × smoothness 1 | 3.261 | 0.325 |
| 基础色 alpha 128 | 3.336 | 0.330 |

结果说明原对照中显示变换是一项显著差异，并不说明 BRDF 完全一致。光照辐射单位仍未独立标定，球的网格、抗锯齿、采样及纹理压缩也不完全相同；引擎仍没有正式画面线性浮点读回，因此这仍是 8 位显示像素比较。以上只适用于不透明受光球、曝光 1、无 Bloom、Reinhard 和 gamma 2.2 的这些场景，不能套用于透明合成、ACES 或不同曝光的场景。

重现例子：

```sh
/Applications/Blender.app/Contents/MacOS/Blender -b -t 1 \
  --python projects/showcase/tools/pbr-comparison/encode_linear_reference.py -- \
  build/material-comparison/expanded/blender-emission-linear.exr \
  build/material-comparison/display-matched/blender-emission.png
/Applications/Blender.app/Contents/MacOS/Blender -b -t 1 \
  --python projects/showcase/tests/pbr_compare_pixels.py -- \
  projects/showcase/cache/material-comparison/engine-emission-standard-v2.png \
  build/material-comparison/display-matched/blender-emission.png \
  build/material-comparison/display-matched/results/emission \
  build/material-comparison/expanded/emission-labels.json
```

十一组转换图、逐球 JSON 和绝对差图保存在 `build/material-comparison/display-matched/`，源 EXR 在 `build/material-comparison/expanded/`；这些都是本地验证产物。AO 只有引擎单侧场景，不在这 66 球中。

## SpecGloss 修正后的补充对照

上表三个 SpecGloss 组使用的是修正前的 `standard-v2` 截图，不能作为当前结果。以下改用此前实际拍摄的 `standard-v3` 引擎截图，与同一批 Blender 线性 EXR 经上述相同显示变换得到的图像比较；没有重拍或改变两侧材质、灯光。单位仍是 0–255 显示编码 RGB 的 MAE。

| 参数 | v3 统一显示变换 MAE |
| --- | ---: |
| IOR 1 / F0 0 | 0.213 |
| IOR 1.2 / F0 0.00826 | 0.400 |
| IOR 1.5 / F0 0.04 | 1.256 |
| IOR 1.8 / F0 0.08163 | 2.383 |
| IOR 2 / F0 0.11111 | 3.163 |
| IOR 2.5 / F0 0.18367 | 4.928 |
| 镜面 RGB 0 / 64 / 128 / 191 / 255 | 0.213 / 0.447 / 0.710 / 0.981 / 1.257 |
| 镜面 RGB 255，关闭直接镜面 | 2.711 |
| smoothness 0 / 0.5 / 1 | 2.023 / 1.257 / 1.202 |
| 镜面 alpha 64 / 191 | 1.631 / 1.180 |
| 基础色 alpha 128 | 1.254 |

灰度 IOR 仅匹配正入射 F0，Blender Principled 和 glTF SpecGloss 的漫反射及掠射响应不完全一致，不能用差值判定哪个实现更正确。关闭直接高光也没有严格等价的 Blender 对照。该组镜面 RGB 图在两侧按线性数据使用，并非标准 SpecGloss 的 sRGB RGB + 线性 alpha 纹理对比；后者目前只有独立引擎 GPU 回归，不能以此表代替。详细前提见 [SpecGloss 复测](specgloss-v3-results.md)。逐项 JSON 和差图在 `build/material-comparison/display-matched/results/*-standard-v3/`。
