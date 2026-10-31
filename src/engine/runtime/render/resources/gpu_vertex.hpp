#pragma once

namespace engine::render_detail
{

struct GpuVertex
{
    float position[3]{};
    float normal[3]{};
    float tangent[4]{};
    float texcoord[2]{};
    float color[4]{};
};

static_assert(sizeof(GpuVertex) == 16 * sizeof(float));

} // namespace engine::render_detail
