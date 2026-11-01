# Voxel Tools 地形连通域参考

`src/engine/plugins/runtime/terrain/terrain_islands.cpp` 的六邻域连通与边界锚定流程参考公开 Voxel Tools 的 `util/island_finder.h` 和 `edition/floating_chunks.cpp`。引擎继续使用自己的 FlatBuffers 数据、Transvoxel 表面提取和 Jolt 碰撞接口，不引入 Godot 模块。

- 仓库：<https://github.com/Zylann/godot_voxel>
- 固定版本：`045ff9326a32fd10ac6fdac6357b5a098aaf2093`
- 原许可：<https://github.com/Zylann/godot_voxel/blob/045ff9326a32fd10ac6fdac6357b5a098aaf2093/LICENSE.md>

以下保留该固定版本的完整许可：

```text
MIT License

Copyright (c) 2016-2024 Marc Gilleron

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
