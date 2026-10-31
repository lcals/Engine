#include "runtime/render/passes/shadow_frame.hpp"

#include "runtime/render/render_math.hpp"

#include <bx/math.h>

#include <cmath>

namespace engine::render_detail
{

bool prepare_shadow_frame(const CameraSettings& camera, const RenderSettings& settings,
                          float aspect, const float* camera_world, const float* camera_view_matrix,
                          const std::array<float, 4>& direction, ShadowFrame& frame)
{
    const auto* caps = bgfx::getCaps();
    const float near_clip = camera.near_clip;
    const float far_clip = std::min(camera.far_clip, settings.shadow_distance);
    const auto cascade_count = settings.shadow_cascades;
    frame.tile_size =
        static_cast<std::uint16_t>(settings.shadow_resolution / (cascade_count == 1 ? 1 : 2));
    frame.parameters = {static_cast<float>(cascade_count), settings.shadow_cascade_blend, near_clip,
                        far_clip};
    frame.splits.fill(far_clip);
    const bx::Vec3 up =
        std::abs(direction[1]) > 0.95F ? bx::Vec3{0.0F, 0.0F, 1.0F} : bx::Vec3{0.0F, 1.0F, 0.0F};
    bx::mtxLookAt(frame.view.data(), {0.0F, 0.0F, 0.0F}, {direction[0], direction[1], direction[2]},
                  up, bx::Handedness::Right);
    for (std::uint16_t index = 0; index < maximum_shadow_cascades; ++index)
    {
        bx::mtxIdentity(frame.matrices[index].data());
    }
    // bgfx examples/16-shadowmaps, commit 7a70927d2af7ffcc5cef9c9e8f4c821e31fcb099:
    // splitFrustum blends logarithmic and uniform splits; orthographic crops are texel-stable.
    // This atlas adaptation uses a frustum sphere and explicit blend overlap.
    // Copyright 2013-2014 Dario Manesku; third_party/notices/bgfx.txt.
    for (std::uint16_t index = 0; index + 1 < cascade_count; ++index)
    {
        const float fraction = static_cast<float>(index + 1) / cascade_count;
        const float logarithmic = near_clip * std::pow(far_clip / near_clip, fraction);
        const float uniform = near_clip + (far_clip - near_clip) * fraction;
        frame.splits[index] = uniform + (logarithmic - uniform) * settings.shadow_split_lambda;
    }
    constexpr float degrees_to_radians = 0.01745329251994329577F;
    const float slope = std::tan(camera.fov_degrees * degrees_to_radians * 0.5F);
    const bool orthographic = camera.projection == schema::Projection::orthographic;
    for (std::uint16_t index = 0; index < cascade_count; ++index)
    {
        float slice_near = index == 0 ? near_clip : frame.splits[index - 1];
        const float slice_far = frame.splits[index];
        if (index > 0)
        {
            const float previous_near = index == 1 ? near_clip : frame.splits[index - 2];
            // The next cascade also covers the previous cascade's blend interval.
            slice_near -= (slice_near - previous_near) * settings.shadow_cascade_blend;
        }
        const float center_depth = (slice_near + slice_far) * 0.5F;
        std::array<float, 3> center{};
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            center[axis] =
                camera_world[12 + axis] - camera_view_matrix[axis * 4 + 2] * center_depth;
        }
        center = transform_point(frame.view.data(), center);
        auto& bounds = frame.bounds[index];
        bounds.reset();
        float radius_squared = 0.0F;
        for (std::uint16_t corner = 0; corner < 8; ++corner)
        {
            const float depth = (corner & 4) != 0 ? slice_far : slice_near;
            const float half_height = orthographic ? camera.orthographic_size : depth * slope;
            const float x = ((corner & 1) != 0 ? half_height : -half_height) * aspect;
            const float y = (corner & 2) != 0 ? half_height : -half_height;
            std::array<float, 3> point{};
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                point[axis] = camera_world[12 + axis] + camera_view_matrix[axis * 4] * x +
                              camera_view_matrix[axis * 4 + 1] * y -
                              camera_view_matrix[axis * 4 + 2] * depth;
            }
            bounds.include(transform_point(frame.view.data(), point));
            const float z = depth - center_depth;
            radius_squared = std::max(radius_squared, x * x + y * y + z * z);
        }
        // A sphere gives rotation-independent XY extent. Reserve three filter texels and
        // half a texel for center snapping so the covered frustum never leaves the tile.
        const float radius = std::ceil(std::sqrt(radius_squared) * 16.0F) / 16.0F;
        const float half_extent = radius / (1.0F - 7.0F / frame.tile_size);
        const float texel_size = (2.0F * half_extent) / frame.tile_size;
        for (std::size_t axis = 0; axis < 2; ++axis)
        {
            const float snapped = std::round(center[axis] / texel_size) * texel_size;
            bounds.minimum[axis] = snapped - half_extent;
            bounds.maximum[axis] = snapped + half_extent;
        }
        const float scale = cascade_count == 1 ? 1.0F : 0.5F;
        const float tile_x = static_cast<float>(index % 2) * scale;
        const float tile_y = static_cast<float>(index / 2) * scale;
        frame.rectangles[index] = {tile_x, caps->originBottomLeft ? 1.0F - tile_y - scale : tile_y,
                                   scale, scale};
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            if (!std::isfinite(bounds.minimum[axis]) || !std::isfinite(bounds.maximum[axis]) ||
                bounds.maximum[axis] <= bounds.minimum[axis])
            {
                return false;
            }
        }
    }
    return finite_matrix(frame.view.data());
}

} // namespace engine::render_detail
