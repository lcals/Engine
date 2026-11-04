#include "runtime/render/mesh_lod_selection.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace engine::render_detail
{

float projected_mesh_height(const std::array<float, 3>& minimum,
                            const std::array<float, 3>& maximum, const float* model,
                            const float* view, const float* projection, float near_clip) noexcept
{
    const float unsafe = std::numeric_limits<float>::infinity();
    if (!model || !view || !projection || !std::isfinite(near_clip) || near_clip <= 0)
    {
        return unsafe;
    }
    const auto transform = [](const float* matrix, const std::array<double, 4>& point)
    {
        std::array<double, 4> result{};
        for (std::size_t row = 0; row < 4; ++row)
        {
            for (std::size_t column = 0; column < 4; ++column)
            {
                result[row] += matrix[column * 4 + row] * point[column];
            }
        }
        return result;
    };
    double low = std::numeric_limits<double>::max();
    double high = std::numeric_limits<double>::lowest();
    for (unsigned int corner = 0; corner < 8; ++corner)
    {
        std::array<double, 4> point{0, 0, 0, 1};
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            if (!std::isfinite(minimum[axis]) || !std::isfinite(maximum[axis]) ||
                minimum[axis] > maximum[axis])
            {
                return unsafe;
            }
            point[axis] = corner & (1U << axis) ? maximum[axis] : minimum[axis];
        }
        const auto camera = transform(view, transform(model, point));
        if (!std::all_of(camera.begin(), camera.end(),
                         [](double value) { return std::isfinite(value); }) ||
            -camera[2] <= near_clip)
        {
            return unsafe;
        }
        const auto clip = transform(projection, camera);
        if (!std::all_of(clip.begin(), clip.end(),
                         [](double value) { return std::isfinite(value); }) ||
            clip[3] <= 0)
        {
            return unsafe;
        }
        const double projected = clip[1] / clip[3];
        low = std::min(low, projected);
        high = std::max(high, projected);
    }
    const double height = (high - low) * 0.5;
    return std::isfinite(height) && height <= std::numeric_limits<float>::max()
               ? static_cast<float>(height)
               : unsafe;
}

std::size_t select_mesh_lod(const schema::MeshDataT& data, float screen_height,
                            std::size_t previous_level, float hysteresis) noexcept
{
    if (!std::isfinite(screen_height) || screen_height < 0 || !std::isfinite(hysteresis) ||
        hysteresis < 0 || hysteresis >= 1 || !data.skin.empty() || !data.skeleton_guid.empty())
    {
        return 0;
    }
    for (const auto& lod : data.lods)
    {
        if (!lod || !std::isfinite(lod->screen_height) || lod->screen_height <= 0)
        {
            return 0;
        }
    }
    std::size_t level = previous_level <= data.lods.size() ? previous_level : 0;
    while (level > 0 && screen_height > data.lods[level - 1]->screen_height * (1 + hysteresis))
    {
        --level;
    }
    while (level < data.lods.size() &&
           screen_height < data.lods[level]->screen_height * (1 - hysteresis))
    {
        ++level;
    }
    return level;
}

} // namespace engine::render_detail
