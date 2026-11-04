# PBR 材质球对照

当前修复与验收范围见[当前结果](pbr-comparison/current-results.md)。扩展参数历史矩阵与逐球统计见 [PBR 参数对照报告](pbr-parameter-comparison.md)；Blender 参考场景及同源网格复现方式见 [工具说明](pbr-comparison/tools.md)。

`scenes/pbr-comparison.scene` 是引擎的 PBR 直射光基线：六个相同球体分别使用金属度 0、1 和粗糙度 0.2、0.5、0.8，使用内置 PBR shader、正式 Mesh、材质和预制体资源。场景不依赖玩法脚本，预览即可观察；场景已加入资源构建入口。

| 参数 | 值 |
| --- | --- |
| 输出 | 1280 × 720；背景黑色 |
| 球体 | 半径 0.9 m，UV 球经纬分段 64 × 32；x = −2.2、0、2.2 m，y = ±1.1 m |
| 相机 | 正交，位置 `(0, 0, 12)`，朝向 −Z，竖直视野 7.2 m；球心对应 `(420, 250)`、`(640, 250)`、`(860, 250)`、`(420, 470)`、`(640, 470)`、`(860, 470)`，理想半径 90 px |
| 材质 | Base Color 在线性空间为 `(0.5, 0.5, 0.5)`；无贴图，表面使用几何法线；上排金属度 0，下排金属度 1，每排粗糙度从左到右为 0.2、0.5、0.8 |
| 光 | 一盏白色方向光，从球指向光源的方向为 `(-4, 10, 7) / √165`，强度 1；无环境光、IBL、阴影、AO 或 SSR |
| 后处理 | 曝光乘数 1；关闭 filmic、HDR 和 Bloom；当前受光物体仍经过 Reinhard 和 gamma 2.2 输出 |

材质球无纹理以隔离直射 BRDF。后续贴图对照应使用同一图像和 UV：Base Color 纹理按 sRGB 解码，金属度、粗糙度和法线均按线性数据读取，不把它们误当 sRGB。

## 预览与截图

先以独立运行目录启动 Release 编辑器，再打开预览：

```sh
build/local-ninja-release/editor_app --project projects/showcase --runtime-dir build/pbr-comparison-runtime --width 640 --height 360
```

```sh
python3 scripts/mcp_client.py --server build/local-ninja-release/mcp_app --project projects/showcase --runtime-dir build/pbr-comparison-runtime call open_scene_preview --arguments '{"path":"scenes/pbr-comparison.scene"}'
python3 scripts/mcp_client.py --server build/local-ninja-release/mcp_app --project projects/showcase --runtime-dir build/pbr-comparison-runtime call capture_frame --arguments '{"path":"material-comparison/engine.png"}'
python3 scripts/mcp_client.py --server build/local-ninja-release/mcp_app --project projects/showcase --runtime-dir build/pbr-comparison-runtime call get_frame_capture
```

`capture_frame` 返回 `pending` 后，以 `get_frame_capture` 为 `complete` 为准。上述 640 × 360 是本次 macOS Retina 2× 的逻辑窗口尺寸，实际帧缓冲区为 1280 × 720；其他显示密度必须按实际 PNG 尺寸调整窗口，不要缩放截图冒充原生分辨率。

## 初版观察与限制

2026-11-04 的引擎实际 GPU 截图位于本机 `cache/material-comparison/engine-1280x720.png`；同次 Blender 参考图位于 `build/material-comparison/blender-material-comparison.png`。两者分别是引擎帧与 Blender Cycles 输出，截图和构建产物不进入版本管理。该基线用于定位差异，不等同两个渲染器的物理响应完全一致：Blender 使用 Standard sRGB 输出，引擎受光结果使用 Reinhard 后 gamma 2.2（见 `src/engine/runtime/render/shaders/hdr/hdr_helpers.sh`），直接像素差会包含显示变换差异。两边的方向光强度数值相同也不等于辐射单位已校准；几何离散、采样、BRDF 和抗锯齿也可能造成差异。

初版截图中，同一行球的高光相对于球心在引擎中随球位置变化，而 Blender 正交相机下基本一致。当时内置与范例 PBR 都以相机位置减去片元位置计算 `view_dir`，未对正交投影使用平行视线；此问题已修复并完成窗口回归。本节保留的是修复前基线，不能将其画面或误差作为当前版本结论。
