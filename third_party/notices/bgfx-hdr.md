# bgfx HDR 与 Bloom 来源和许可

HDR 场景合成与 Bloom 适配 bgfx 官方 `examples/09-hdr`，固定来源为
[`7a70927d2af7ffcc5cef9c9e8f4c821e31fcb099`](https://github.com/bkaradzic/bgfx/tree/7a70927d2af7ffcc5cef9c9e8f4c821e31fcb099/examples/09-hdr)，
与项目使用的 bgfx.cmake `v1.129.8940-496` 对应。作者为 Branimir Karadzic，
许可为 BSD-2-Clause。

适配文件位于 `src/engine/runtime/render/shaders/hdr/`：

- `vs_hdr.sc` 与 `hdr_varying.def.sc`：共享全屏三角形，UV 翻转由调用方按后端处理。
- `fs_hdr_bright.sc`：九次采样的亮部提取；改为以当前像素为中心的对称 3×3 核。
- `fs_hdr_blur.sc`：垂直 `blur9`。
- `fs_hdr_resolve.sc`：水平 `blur9`、HDR 合成、曝光和最终色调映射。
- `hdr_helpers.sh`：保留上游 `common.sh` 的 `blur9` 权重
  `1 / 0.9 / 0.55 / 0.18 / 0.1`，以 `4.46` 归一化；色调映射使用已有的
  Reinhard 及上游 `examples/common/shaderlib.sh` 的 ACES 拟合。

CPU 调度和目标生命周期参考同目录 `hdr.cpp`，使用独立场景目标、亮部目标、
垂直模糊目标及最终合成通道。为适配引擎的现有场景参数，作出以下调整：

- 场景和 Bloom 中间目标改用线性 RGBA16F，不沿用 RGBE8 编解码。
- 阈值在曝光前的线性颜色上计算，移除示例亮部提取阶段的色调映射和 gamma。
- 合成线性 Bloom 后仅执行一次曝光、Reinhard/ACES 和 gamma，保留已有 SDR 输出方式。
- 使用调用方明确传入的曝光，不引入示例的亮度测量、自动曝光或亮度平均贴图。
- Bloom 强度和半径来自场景参数，两轴步长使用相同缩小目标尺寸和半径。
- Uniform 使用 `u_hdr_params`、`u_hdr_texel`，采样器使用 `s_hdr_scene`、`s_hdr_bloom`，
  与项目材质参数隔离。UI 在合成后绘制；场景字体先输出线性预乘颜色。

```text
Copyright 2011-2025 Branimir Karadzic. All rights reserved.
Copyright 2010-2025 Branimir Karadzic

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

   1. Redistributions of source code must retain the above copyright notice, this
      list of conditions and the following disclaimer.

   2. Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
OF THE POSSIBILITY OF SUCH DAMAGE.
```
