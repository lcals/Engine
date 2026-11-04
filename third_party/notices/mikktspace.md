# MikkTSpace

编辑器的 FBX、普通网格和建模导入通过 vcpkg 使用 MikkTSpace，源码固定提交
`3e895b49d05ea07e4c2133156cfa94369e19e409`：
<https://github.com/mmikk/MikkTSpace/tree/3e895b49d05ea07e4c2133156cfa94369e19e409>。
本项目在 `third_party/overlay-ports/mikktspace/small-angle-precision.patch` 中修复极细长
三角形的角度加权：当单精度点积舍入为 1 时，使用叉积长度和 `atan2` 保留非零夹角，其他情况保留
上游 `acos` 路径。共用适配代码位于 `src/engine/editor/mesh/mesh_tangents.cpp`。本地 port 基于
vcpkg `2020-10-06#3`，补丁后为 `2020-10-06#4`。上游许可证（来自 `mikktspace.h`）：

```text
Copyright (C) 2011 by Morten S. Mikkelsen

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
```
