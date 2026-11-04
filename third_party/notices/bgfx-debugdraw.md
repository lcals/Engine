# bgfx 调试绘制与线框

来源：bgfx.cmake `1.167.9605-606` 中的 bgfx
`b2513038f4b753ebeced40821c193978daa0492f`。

- `examples/28-wireframe/vs_wf_vertex_pulling.sc`：屏幕空间三角形边展开；引擎适配实际
  32 位索引、顶点 stride、主场景视图及 HDR 叠加，保留 Shader 来源声明。
- `examples/common/debugdraw` 与 `packrect.h`：使用固定版本上游 DebugDrawEncoder；
  独立编译该模块，不依赖 example entry、窗口或 ImGui 框架。

Copyright 2011-2026 Branimir Karadzic. All rights reserved.
Copyright 2016 Dario Manesku. All rights reserved.
Copyright 2026 Alessandro Muntoni. All rights reserved.

BSD 2-Clause License

Redistribution and use in source and binary forms, with or without modification, are permitted
provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions
   and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions
   and the following disclaimer in the documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
