# vcpkg port 与补丁许可

`third_party/overlay-ports/basisu/` 中的 port 定义与 `export-cmake-config.diff` 基于 Microsoft vcpkg 的 [2.50#1 port](https://github.com/microsoft/vcpkg/tree/149fdb9c7b51a03cd54faf1766774b43366b0534/ports/basisu)，固定提交为 `149fdb9c7b51a03cd54faf1766774b43366b0534`。本地移动端与输出适配见 [overlay 说明](../overlay-ports/README.md)。库本身的许可由 port 安装，不以此 MIT 许可替代。

`third_party/overlay-ports/spirv-cross/` 的 port 定义复制自当前 baseline 对应的 vcpkg port 树 `3246574c039c7c7b5c1fc8c4f5ba6aac45e3f4ab`，版本仍为 `1.4.309.0`。本地仅登记 PointSize 查询兼容补丁；补丁采用的 SPIRV-Cross 代码单独遵循 [SPIRV-Cross 许可说明](spirv-cross.md)。

以下保留上游 MIT 许可全文：

```text
MIT License

Copyright (c) Microsoft Corporation

Permission is hereby granted, free of charge, to any person obtaining a copy of this
software and associated documentation files (the "Software"), to deal in the Software
without restriction, including without limitation the rights to use, copy, modify,
merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to the following
conditions:

The above copyright notice and this permission notice shall be included in all copies
or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```
