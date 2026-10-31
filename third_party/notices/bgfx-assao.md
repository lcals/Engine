# bgfx ASSAO 来源与许可

运行时屏幕空间环境遮蔽适配 bgfx 官方 `examples/39-assao`，固定来源为
[`7a70927d2af7ffcc5cef9c9e8f4c821e31fcb099`](https://github.com/bkaradzic/bgfx/tree/7a70927d2af7ffcc5cef9c9e8f4c821e31fcb099/examples/39-assao)，
与项目使用的 bgfx.cmake `v1.129.8940-496` 对应。示例作者为 Attila Kocsis；
原示例将 Intel 的 [ASSAO](https://github.com/GameTechDev/ASSAO) 列为算法参考。

保留的 Shader 文件位于 `src/engine/runtime/render/shaders/assao/`：

- `cs_assao_prepare_depths_and_normals.sc`：深度分离与几何法线重建。
- `cs_assao_prepare_depth_mip.sc`：按深度差加权生成 mip。
- `cs_assao_generate_q2.sc`、`cs_assao_generate_q.sh`：固定 Q2 的采样图案、遮蔽与边缘信息。
- `cs_assao_smart_blur.sc`：边缘感知过滤，调度执行两次。
- `cs_assao_apply.sc`：合并四个分离层并生成全尺寸遮蔽结果。
- `uniforms.sh`：保留上游 19 个 vec4 的参数布局。

CPU 调度参考同目录的 `assao.cpp`。适配保持上游采样图案、深度 mip 权重、边缘检测和过滤算法，
只接入本引擎的资源生命周期及前向渲染：

- 引擎用实际材质的深度预通道保留自定义顶点变换和 alpha 裁切，再由深度重建法线。
- 计算 Uniform 改为 `u_assao_params`、`u_assao_rect`，避免与项目材质名称冲突。
- 在原空闲参数中保存正交标记，补齐正交深度、视空间位置、像素尺度、平行观察方向和精度偏移。
- 法线图边界读取限制在有效像素内，避免访问纹理外部；零向量除法加保护。
- R16F 中间深度限制为有限值，最远为 65504，避免大远裁面和背景像素生成无穷值。
- 保留线性遮蔽值，移除示例最终输出的 gamma 转换；前向材质只用结果调制环境光和 IBL，
  不乘直接光、发光或无光照颜色。
- 只构建固定 Q2 链，不编译自适应重要性图及其他质量级别的入口。

以下保留上游示例作者版权声明及 bgfx 的 BSD-2-Clause 许可全文。各 Shader 文件继续保留原作者声明。

```text
Copyright 2018 Attila Kocsis. All rights reserved.
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
