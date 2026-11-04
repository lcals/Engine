# Filament 全景图采样参考

全景图转立方图时按输出 texel 在全景图中的投影范围估算采样数，并使用低差异序列采样输出足迹的做法，参考 Google Filament 的 `CubemapUtils::equirectangularToCubemap`：

https://github.com/google/filament/blob/5ce4cbf19051729d74db9ba33ad5bd04cf6b4f37/libs/ibl/src/CubemapUtils.cpp

Copyright (C) 2015 The Android Open Source Project. Licensed under the Apache License, Version 2.0；许可证全文见 [Apache License 2.0](the-forge.txt)。

本项目针对 GPU compute 和既有 cubemap 方向约定适配；添加经度接缝展开、立体角权重和有限采样上限。此为近似面积采样，不保证保留所有亚像素高亮。
