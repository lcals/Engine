# bgfx 大气天空与 Hex-Tiling

固定上游：bgfx `b2513038f4b753ebeced40821c193978daa0492f`（bgfx.cmake
`1.167.9605-606`，现有 overlay 固定归档 SHA512）。

- `examples/53-sky2/atmosphere.sh`、`cs_atmo_transmittance.sc`、
  `cs_atmo_multiscatter.sc`、`cs_atmo_skyview.sc`、`vs_sky2_bg.sc`、`fs_sky2_bg.sc`：
  Copyright 2026 Mateusz Kozdrowicki，BSD-2-Clause。
- `examples/49-hextile/fs_hextile.sc` 与 `examples/53-sky2/hextile.sh`：
  Copyright 2022 Preetish Kakkar，BSD-2-Clause。

适配保留上游大气相函数、密度分布、LUT 映射和积分。变更为场景所有权、米到千米换算、
独立视图 LUT、独立 uniform 名称、平台深度约定及线性 HDR MRT 输出；去掉示例自带曝光和
色调映射，由引擎统一显示 resolve。未移植示例应用、地形照明、示例阴影和空气透视。

Hex 基于上游网格、旋转、哈希、显式梯度和对比权重。PBR 贴图共享同一权重；ORM 保持
线性，法线以表面梯度旋转/混合，不使用示例的颜色着色替代 PBR。

参考论文：Sébastien Hillaire, *A Scalable and Production Ready Sky and Atmosphere
Rendering Technique*, EGSR 2020：<https://sebh.github.io/publications/egsr2020.pdf>。

## bgfx 许可

```text
Copyright 2010-2026 Branimir Karadzic

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

## mmikk 法线梯度参考

固定来源：<https://github.com/mmikk/hextile-demo/tree/43c3ed7e18e1e4539fa9323f72d5e2a65147ebb3>，
`hextile-demo/hextiling.h` 的 `bumphex2derivNMap` 及 `surfgrad_framework.h` 的
`TspaceNormalToDerivative`。适配改为本引擎完整 XYZ 法线约定，所有 PBR 通道共用颜色生成的
权重；不迁入其场景、美术资产、直方图工具或渲染框架。

```text
MIT License

Copyright (c) 2022 mmikk

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
