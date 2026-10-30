# Ant IBL GPU 烘焙 Shader

源自本地参考 Ant `pkg/ant.resources/shaders/pbr/ibl/` 与 `pkg/ant.sky/assets/cs_panorama2cubemap.sc`、`pkg/ant.resources/shaders/common/utils.sh`。
上游：https://github.com/ejoy/ant 。原始数学实现与来源注释保留在 `ibl_common.sh`。

适配：每面使用2D UAV；增加非8倍数边界检查；LUT采样纹素中心避免NdotV=0奇点；预过滤按累计NoL权重归一化（与The Forge PBR计算范例一致），保留常色环境亮度；宿主用bgfx compute与逐面blit/readback调度。

`ibl_common.sh` 上游还注明 Khronos glTF Sample Viewer IBL filtering、Holger Dammertz 的 Hammersley 采样（CC BY 3.0，http://holger.dammertz.org/stuff/notes_HammersleyOnHemisphere.html ；许可 https://creativecommons.org/licenses/by/3.0/）。这些算法来源及链接随源文件保留；适配不改变上游归属。

The MIT License (MIT)

Copyright (c) 2017-2024 lingxigames.com

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
