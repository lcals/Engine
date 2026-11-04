# Ray Tracing Gems：法线贴图地平线平滑

内置 PBR 与公开范例的直接漫反射使用《Ray Tracing Gems》第 12 章
“A Microfacet-Based Shadowing Function to Solve the Bump Terminator Problem”
配套代码中的 `bump_alpha2` 和 `bump_shadowing_function` 方法。

- 作者：Alejandro Conty Estevez、Pascal Lecocq、Clifford Stein。
- 配套代码仓库：<https://github.com/Apress/ray-tracing-gems>。
- 固定提交：`424bf8d58f9e382e8d71ab21e4c0a5a5684669d2`。
- [原始实现](https://github.com/Apress/ray-tracing-gems/blob/424bf8d58f9e382e8d71ab21e4c0a5a5684669d2/Ch_12_A_Microfacet-Based_Shadowing_Function_to_Solve_the_Bump_Terminator_Problem/terminator.cpp)。
- [仓库许可声明](https://github.com/Apress/ray-tracing-gems/blob/424bf8d58f9e382e8d71ab21e4c0a5a5684669d2/README.md#license)与
  [完整许可证](https://github.com/Apress/ray-tracing-gems/blob/424bf8d58f9e382e8d71ab21e4c0a5a5684669d2/LICENSE.txt)。

仓库声明章节代码默认采用 MIT，除非该章节文件另有说明；本章的
`terminator.cpp` 没有另行许可。本项目适配的是这份 MIT 配套代码，
不以出版社论文的 CC BY-NC-ND 文本许可代替代码许可。

## 适配与范围

实现位于 `src/engine/runtime/render/shaders/pbr_helpers.sh`，
由同目录 `pbr_lighting.sh` 使用，公开范例保持同样逻辑。
输入是插值后的平滑基础法线、法线贴图扰动后的着色法线和来光方向。
保留上游由法线夹角确定遮蔽粗糙度的系数 `0.125` 及其 `[0, 1]` 范围；
该系数来自上游方法，不由材质球截图拟合。

为适配 bgfx Shader 和有限精度计算，使用 GLSL 风格类型、现有 Shader 函数，
对单位向量点积做数值范围保护；阴影因子使用与上游代数等价的形式，
并显式处理无扰动法线和零余弦，避免除零或 `0/0`。
该因子只衰减直接漫反射，镜面 BRDF 保持原有求值；
原有基础表面背光可见性检查继续独立执行。

这是法线贴图细节在基础表面地平线附近的微表面遮蔽近似，
不代表真实位移、逐三角形射线可见性或 glTF 规定的唯一校正方法。
预卷积的环境光不能直接用某个单一来光方向的因子作同样处理。

对照实现的适用范围可参考
[Cycles `bsdf.h`](https://github.com/blender/blender/blob/fbe6228777e7d9afefcd61a413844e790ae75db7/intern/cycles/kernel/closure/bsdf.h)
（`v5.2.0`，提交 `fbe6228777e7d9afefcd61a413844e790ae75db7`）：
其漫反射凹凸校正使用同一研究方法。
该文件本身标注 `Apache-2.0` 与 `2011-2022 Blender Foundation`，
不能因为 Blender 主程序采用 GPL 就把该文件也描述成 GPL。
本实现的源码来源与许可仍是上述固定版本的 MIT 配套代码。

## 上游 MIT 许可证

```text
MIT License https://opensource.org/licenses/MIT

Copyright 2019 NVIDIA

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
documentation files (the "Software"), to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and
to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of
the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
```
