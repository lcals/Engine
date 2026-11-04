# Blender PBR 参考场景复现

当前 Release 编辑器批量采集可用 `python3 projects/showcase/tools/pbr-comparison/capture_cases.py`；
输出独立 `report.json`，由 `compare_cases.py`（Blender Python）比较，再由 `write_report.py` 生成嵌入原图的单文件 HTML。
完整复现命令与结果见 [当前结果](current-results.md)。这三个步骤不修改场景或自动调节参数。

当前修复、验证结果和未覆盖范围以[当前结果](current-results.md)为入口。历史记录保留在 [standard-v2 复测](standard-v2-results.md)、[统一显示变换对照](display-matched-results.md)和 [SpecGloss v3](specgloss-v3-results.md)；它们使用不同阶段的引擎截图，不能混作当前版本一次完整验收。

当前输出使用精确 sRGB。生成匹配参考时，把已有线性 EXR 转到独立目录：

```sh
/Applications/Blender.app/Contents/MacOS/Blender -b -t 1 --python-exit-code 1 \
  --python projects/showcase/tools/pbr-comparison/encode_linear_reference.py -- --all
```

结果位于 `build/material-comparison/display-srgb/`；旧 `display-matched/` 是 gamma 2.2 历史参考，转换脚本拒绝覆盖它。采集清单记录 `display_transfer` 以及颜色转换 Shader 指纹；报告对没有该字段的旧清单继续标记 gamma 2.2。对比须在生成所需参考后重新采集，不能混用两个目录。

引擎侧的测试材质、纹理、预制体及场景可用以下脚本重新生成；脚本使用固定 GUID，并保留内容未变的文件：

```sh
python3 projects/showcase/tools/pbr-comparison/generate_engine_cases.py
```

在仓库根目录执行，使用已安装的 Blender（示例为 macOS）：

```sh
/Applications/Blender.app/Contents/MacOS/Blender -b -t 8 \
  --python projects/showcase/tools/pbr-comparison/create_blender_comparison.py -- \
  basecolor metallic roughness normal-orm emission alpha specular-ior

/Applications/Blender.app/Contents/MacOS/Blender -b -t 8 \
  --python projects/showcase/tools/pbr-comparison/create_blender_comparison.py -- uv

PBR_UV_RAW=1 /Applications/Blender.app/Contents/MacOS/Blender -b -t 8 \
  --python projects/showcase/tools/pbr-comparison/create_blender_comparison.py -- uv

/Applications/Blender.app/Contents/MacOS/Blender -b -t 8 \
  --python projects/showcase/tools/pbr-comparison/create_blender_comparison.py -- normal-orm-same-mesh

/Applications/Blender.app/Contents/MacOS/Blender -b -t 8 \
  --python projects/showcase/tools/pbr-comparison/create_blender_comparison.py -- normal-orm-gltf-same-mesh

/Applications/Blender.app/Contents/MacOS/Blender -b -t 1 \
  --python projects/showcase/tools/pbr-comparison/compare_tangent_frames.py
```

输出是 `build/material-comparison/expanded/blender-<case>.png`、同名 `.blend` 和 `-linear.exr`，可用 `PBR_OUTPUT_DIR` 指定其他目录。原生输出 1280×720；后台 Blender 使用独立进程，不改动用户在 GUI 中打开的文件。UV 测试以引擎 mesh 的顶点、法线、索引和 UV 重建；默认根据图像采样原点将 UV 变换后的 V 翻转，`PBR_UV_RAW=1` 保留未经翻转的结果和独立文件名。其他组默认使用 Blender 原生球体；`normal-orm-same-mesh` 用引擎原网格重跑法线组。纹理取自 `projects/showcase/assets/textures/pbr-test-*.png`，其中颜色纹理使用 sRGB、法线和 ORM 使用 Non-Color。

`normal-orm-same-mesh` 保留 Blender 原生 Normal Map Strength 用法；`normal-orm-gltf-same-mesh` 是单独的标准契约对照：先对 Non-Color 图像采样解码后的 XY 乘 `normalTexture.scale`，保留 Z，再以单位强度的 Blender Normal Map 节点变换和归一化；图像节点以等价公式 `scale * (RGB - 0.5) + 0.5` 预处理 RG。两组不互相覆盖；仅修改参考场景的参数表达方式，不修改引擎贴图或调节截图来凑结果。Khronos [glTF 2.0 normalTextureInfo schema](https://github.com/KhronosGroup/glTF/blob/main/specification/2.0/schema/material.normalTextureInfo.schema.json) 定义缩放作用于重建法线的 XY。[Blender 5.2 Cycles 的 Normal Map 实现](https://github.com/blender/blender/blob/v5.2.0/intern/cycles/kernel/osl/shaders/node_normal_map.osl) 的 Strength 包含插值，并不总与该 scale 等价。

切线脚本逐角比较 Blender `mesh.calc_tangents` MikkTSpace 与独立复现的历史 CPU 回退算法，输出角度分布、手性差异和退化 UV 数，供历史诊断使用。当前 FBX、普通 `.mesh` 导入及建模编译已接入编辑器侧 MikkTSpace，运行时动态网格仍使用既有路径。该脚本不是引擎 GPU buffer 回读，也不能据此宣称两个切线管线整体等价。Cycles、引擎的 BRDF、光照单位和显示变换可能不同；法线切线、贴图压缩、采样及锐利边缘也会影响截图。Alpha 组模拟的是 opaque + 阈值裁切，不是透明混合；ORM 的 AO 没有 Blender Principled 的直接同义输入。具体案例与局限另见[PBR 对照说明](../pbr-comparison.md)。

额外测试指定以下组名：

```sh
/Applications/Blender.app/Contents/MacOS/Blender -b -t 8 \
  --python projects/showcase/tools/pbr-comparison/create_blender_comparison.py -- \
  specular-map specular-map-approx smoothness-source alpha-map
```

`specular-map` 将灰度镜面纹理乘 0.04，以 `IOR=(1+sqrt(F0))/(1-sqrt(F0))` 反解 Blender 的正入射反射率；第六球保留常规 IOR=1.5。这组历史夹具的 RGB 按线性数据读取，不是标准 SpecGloss 的 sRGB RGB + 线性 Alpha 贴图对照。`specular-map-approx` 仅将第六球改为 IOR=1，与引擎单独关闭直射镜面并非数学等价。`smoothness-source` 从同一张灰度镜面图或基础色图的 Alpha 读取对应通道，用 `roughness=1-smoothness` 建立参考；引擎会将粗糙度限制在不低于 0.045。`alpha-map` 在同一批基础色图的 Alpha 上执行小于 0.5 裁切，参考未经过引擎 UASTC 压缩，阈值附近需考虑压缩造成的差异。
