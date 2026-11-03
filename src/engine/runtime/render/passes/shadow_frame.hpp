#pragma once

#include "runtime/render/render_constants.hpp"
#include "runtime/render/render_settings.hpp"
#include "runtime/scene/scene_types.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <vector>

namespace engine::render_detail
{

// Four float4 values shared by the compute and material programs.
// kind_inner.z is the first local shadow record + 1 (zero means unshadowed).
// kind_inner.w is the number of local faces (one for spot, six for point).
struct ClusterLight
{
    std::array<float, 4> position_range{};
    std::array<float, 4> direction_outer{};
    std::array<float, 4> color_intensity{};
    std::array<float, 4> kind_inner{};
};

struct ShadowRecord
{
    std::array<float, 16> matrix{};
    std::array<float, 4> rectangle{};
    // Bias, reciprocal atlas size, near, far.
    std::array<float, 4> parameters{};
};

struct LocalShadowLight
{
    std::uint64_t node_id = 0;
    std::uint32_t light_index = 0;
    LightSettings settings{};
    std::array<float, 16> world{};
};

struct ShadowBounds
{
    std::array<float, 3> minimum{};
    std::array<float, 3> maximum{};

    void reset() noexcept
    {
        minimum.fill(std::numeric_limits<float>::max());
        maximum.fill(std::numeric_limits<float>::lowest());
    }

    void include(const std::array<float, 3>& point) noexcept
    {
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            minimum[axis] = std::min(minimum[axis], point[axis]);
            maximum[axis] = std::max(maximum[axis], point[axis]);
        }
    }

    [[nodiscard]] bool overlaps_xy(const ShadowBounds& other) const noexcept
    {
        return minimum[0] <= other.maximum[0] && maximum[0] >= other.minimum[0] &&
               minimum[1] <= other.maximum[1] && maximum[1] >= other.minimum[1];
    }
};

struct ShadowFrame
{
    std::array<float, 16> view{};
    std::array<std::array<float, 16>, maximum_shadow_cascades> matrices{};
    std::array<std::array<float, 4>, maximum_shadow_cascades> rectangles{};
    std::array<ShadowBounds, maximum_shadow_cascades> bounds{};
    std::array<float, 4> splits{};
    std::array<float, 4> parameters{};
    std::uint16_t tile_size = 0;
    std::vector<ShadowRecord> local_records;
    std::uint32_t skipped_local_lights = 0;
};

[[nodiscard]] bool prepare_shadow_frame(const CameraSettings& camera,
                                        const RenderSettings& settings, float aspect,
                                        const float* camera_world, const float* camera_view_matrix,
                                        const std::array<float, 4>& direction, ShadowFrame& frame);

} // namespace engine::render_detail
