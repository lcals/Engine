# 标准 PBR：当前实现与统一复核结果

2026-11-04，当前工作分支 `codex/pbr-standard-calibration` 的复核记录。
本页是当前入口；`standard-v2-results.md`、`specgloss-v3-results.md`、
`display-matched-results.md` 和 `normal-findings.md` 保留分阶段结果，不能混用为同一次验收。

## 默认流程与本次修复

- 默认受光材质为 `builtin://materials/default-pbr`，Shader 为 `builtin://shaders/pbr`，
  metallic/roughness 工作流。新 Mesh、新材质槽、FBX 缺省映射、USD、模型预制体、
  地形和破坏碎块均使用此默认入口；没有自动选择 Blinn/Unlit 的生产回退。
- 公寓和庭院中 11 份实体材质改用 PBR：10 份灯光表面将原 RGB 原值作为线性发光，
  1 份窗户保留基础色并使用不透明 PBR。生成脚本同步。窗口不是透射玻璃；
  发光表面不自动创建照亮周围的 Light。UI、字体、调试标记和明确选择的非 PBR 效果保持各自用途。
- 复核后保留 GGX 稳定分母、相关 Smith、Schlick Fresnel、完整金属/介质混合、
  SpecGloss 漫反射契约，以及 Khronos IBL Fresnel/多散射。没有根据截图退回旧经验公式。
- 法线贴图原背光保护补充 [Ray Tracing Gems 第 12 章](../../../../third_party/notices/ray-tracing-gems-terminator.md)
  的遮蔽近似，仅用于直接漫反射及其烘焙捕获；固定上游系数，无截图拟合。
  镜面、金属高光和预卷积 IBL 不误乘。内置与范例 Shader 同步。
- 关闭 IBL 时，SSR 与主 PBR 统一使用 DFG 回退，已加载的旧 LUT 不再隐式改变 SSR。
- FBX 保留有效源切线，缺失/无效时用固定上游 MikkTSpace；按需拆点保留 UV、颜色、蒙皮和子网格。
  补齐有限 float 输入校验、上游索引界限和完整转换成功后发布，失败保留原完整结果。
  普通 `.mesh`、动态和建模网格仍有各自切线路径，不称为所有网格已统一 MikkTSpace。

之前将 Cycles 指定文件笼统称为 GPL、并据此认为没有可复用实现的判断已纠正：
该文件为 Apache-2.0，本次采用的 RTG 配套代码为 MIT，固定提交和完整许可已保留。

## 同分辨率、同参数复拍

Release 编辑器通过 MCP 打开原场景，12 组全部实际截图为 **1280×720**，
捕获回执 complete，编辑器通过 `close_editor` 退出码 0。
11 组有 Blender 参考，共 66 球；AO 另作引擎侧直接/间接光回归。

两侧保留原材质、光照和相机。Blender 参考保留默认凹凸校正；
从已有线性 EXR 使用相同曝光 1、Reinhard 和 gamma 2.2 得到显示编码参考。
没有改变引擎曝光、法线强度或材质来降低差异。
每球统计中心半径 72 像素，MAE 单位为显示编码 RGB 0–255：

| 参数 | 旧标准直接光阶段 | 仅背光保护 | 当前平滑遮蔽 |
| --- | ---: | ---: | ---: |
| 平法线 | 0.276 | 0.276 | 0.277 |
| 法线强度 0 | 0.164 | 0.164 | 0.164 |
| 法线强度 0.5 | 2.855 | 1.200 | **0.288** |
| 法线强度 1 | 6.705 | 2.780 | **0.469** |
| ORM G 64 | 0.307 | 0.307 | 0.308 |
| ORM G 191 | 0.551 | 0.551 | 0.552 |

其余组的当前逐球 MAE（按画面从左到右、从上到下）：

| 组 | 六球 MAE |
| --- | --- |
| 基础色/颜色贴图 | 0.243 / 0.297 / 0.332 / 0.268 / 0.268 / 0.252 |
| 金属度/ORM B | 0.297 / 0.431 / 0.605 / 0.838 / 1.202 / 1.201 |
| 粗糙度 | 0.314 / 0.287 / 0.321 / 0.295 / 0.656 / 0.946 |
| 发光 | 0.327 / 0.037 / 0.025 / 0.019 / 0.225 / 0.111 |
| UV 缩放/偏移 | 0.530 / 0.570 / 0.824 / 0.377 / 0.272 / 0.377 |
| 常量 Alpha 裁切 | 0 / 0.297 / 0.296 / 0.295 / 0.296 / 0.296 |
| 贴图 Alpha 裁切 | 0 / 0 / 0.296 / 0.295 / 0.296 / 0.296 |
| SpecGloss / 正入射 IOR 近似 | 0.214 / 0.401 / 1.257 / 2.384 / 3.164 / 4.929 |
| 旧线性灰度镜面贴图 | 0.214 / 0.448 / 0.711 / 0.982 / 1.258 / 2.712 |
| 光滑度及来源 | 2.024 / 1.258 / 1.203 / 1.632 / 1.181 / 1.255 |

SpecGloss 三组仍存在明显差异：Blender Principled 与归档 glTF SpecGloss 只在灰度正入射 F0 对应，
并非相同漫反射/掠射模型；关闭直接高光也没有严格等价参考。
旧镜面 RGB 夹具是线性数据，不能充当标准 sRGB RGB + 线性 alpha 纹理对照。
后者由真实 GPU 测试独立检查。以上数字不证明两个渲染器逐像素一致。

本次完整收据、逐球 MAE/RMSE/亮度、原图和差图：
`build/material-comparison/current/1793765746483083000/`。
其中 `review.html` 是嵌入全部截图与表格的单文件报告；
`comparisons.json` 保存完整数值，`report.json` 保存 Shader 指纹和编辑器回执。
源引擎截图在 `projects/showcase/cache/material-comparison/current-1793765746483083000/`。

## 构建与功能回归

macOS Debug/Release 的受影响 editor、渲染、FBX 及场景测试目标编译成功；Debug 不运行测试。
Release `engine.render_foundation`、`engine.window_render`、`editor.fbx_pipeline`、
`editor.fbx_bind_pose` 首批 **4/4** 通过；默认入口补充断言的 `engine.scene` 和 `editor.fbx_pipeline` **2/2** 通过，共 5 项不同测试。

真实 GPU 新增检查包含法线强度 0/1、来光角 0°/60°/85°/89°/91°、双面反向、纯金属镜面隔离。
十个直接漫反射像素与独立原始 RTG 公式的显示值差均小于 1/255；
例如强度 1、85°为 98（参考 98.25），89°为 52（参考 51.60）。
纯金属为 109（参考 108.75）。IBL 关闭时保留/移除 LUT 引用的整图最大通道差为 **0**。
此前 AO、低粗糙度高光、Fresnel、SpecGloss 颜色空间、白炉 IBL 和 SSR 回归仍通过。

公寓、庭院新 PBR 材质完成正式导入和 Release 实窗预览，1280×720 截图 complete，
编辑器正常退出。收据位于 `build/material-comparison/default-pbr-preview/1793765982824043000/`。

已有 IBL 烘焙、NPOT mip 和全景亮斑验证见[测试文档](../../../../docs/testing.md)。
这次审查未发现新的确定错误，未将固定采样下的近似误差包装成完全收敛。

## 复现及边界

```sh
python3 projects/showcase/tools/pbr-comparison/capture_cases.py
# 使用上一条命令输出的 report.json；Blender 路径由本机安装位置决定。
blender -b -t 2 --python-exit-code 1 \
  --python projects/showcase/tools/pbr-comparison/compare_cases.py -- <report.json>
python3 projects/showcase/tools/pbr-comparison/write_report.py <report.json>
```

前提是已经按 [README](README.md) 生成 Blender EXR 和统一显示编码参考。
脚本不覆盖旧截图，不自动调整任何参数；缺少参考或分辨率不同会失败。
只复拍法线组可以给 `capture_cases.py` 传 `normal-orm`。

正式画面仍缺线性浮点 HDR 回读；8 位截图的逆显示变换不是 HDR 验收。
高对比 IBL 有限采样、SSR 屏幕外缺失及多散射残项近似、网格/抗锯齿差异仍存在。
灯光辐射单位未独立标定，也未实测 Windows/Android/iOS。
这些限制保留在报告中，不通过调亮材质、修改法线或关闭参考校正来掩盖。
