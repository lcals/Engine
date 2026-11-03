# Ant clustered lighting

Source: https://github.com/ejoy/ant/tree/f32fbd4811e90c2433bf159aa40fb8f8aceb8b58

Adapted files:
- `pkg/ant.resources/shaders/compute/cs_cluster_aabb.sc`
- `pkg/ant.resources/shaders/compute/cs_lightcull.sc`
- `pkg/ant.resources/shaders/common/cluster_shading.sh`

Changes: C++/bgfx ownership, a fused bounds and culling dispatch, right-handed camera
coordinates, perspective/orthographic projections, partial edge tiles, conservative
spot-light bounds, explicit zero-light clearing, bounded indices and a full-light
fallback for overflowing clusters. Directional lights remain a separate fragment
loop. Transparent fragments use the complete view-frustum grid.

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
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
