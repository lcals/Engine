# vcpkg port 与补丁许可

`third_party/overlay-ports/basisu/` 中的 port 定义与 `export-cmake-config.diff` 基于 Microsoft vcpkg 的 [2.50#1 port](https://github.com/microsoft/vcpkg/tree/149fdb9c7b51a03cd54faf1766774b43366b0534/ports/basisu)，固定提交为 `149fdb9c7b51a03cd54faf1766774b43366b0534`。本地移动端与输出适配见 [overlay 说明](../overlay-ports/README.md)。库本身的许可由 port 安装，不以此 MIT 许可替代。

2026-11-04 更新的 `sdl3`、`tbb`、`quickjs-ng`、`usd` port 基于 Microsoft vcpkg 固定提交 `15b12b99a227d51e21c86dffd40385439eac87a0` 的同名目录，迁移到各自更新的正式发布版。新增 `flecs`、`efsw`、`flatbuffers`、`tinyexr` port 基于新 baseline 的同名目录；归档版本和 SHA512 见对应 portfile。库本身的许可及必要第三方声明由各 port 的 `vcpkg_install_copyright` 安装。

旧 `spirv-cross` overlay 随 baseline 升到 SDK `1.4.363.0` 删除：PointSize 查询已由上游正式版提供。此前适配来源及许可继续保留在 [SPIRV-Cross 说明](spirv-cross.md)。

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
