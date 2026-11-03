#include "runtime/render/passes/planar_reflection_math.hpp"
#include "runtime/render/render_math.hpp"

#include <array>
#include <bx/math.h>
#include <cmath>
#include <cstdio>
#include <limits>

namespace
{
bool expect(bool value, const char* message)
{
    if (!value)
    {
        std::fprintf(stderr, "planar reflection: %s\n", message);
    }
    return value;
}

std::array<float, 4> clip(const std::array<float, 16>& matrix, const std::array<float, 3>& position)
{
    std::array<float, 4> result{};
    for (std::size_t row = 0; row < 4; ++row)
    {
        result[row] = matrix[row] * position[0] + matrix[4 + row] * position[1] +
                      matrix[8 + row] * position[2] + matrix[12 + row];
    }
    return result;
}

bool verify(bool homogeneous, bool orthographic, bool tilted)
{
    std::array<float, 16> plane{}, camera{}, input{}, world{}, view{}, projection{}, combined{};
    bx::mtxRotateZ(plane.data(), tilted ? 0.35F : 0.0F);
    const std::array<float, 3> normal{plane[4], plane[5], plane[6]};
    plane[12] = 2.0F;
    plane[13] = 3.0F;
    plane[14] = -1.0F;
    bx::mtxIdentity(camera.data());
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        camera[12 + axis] = plane[12 + axis] + normal[axis] * 5.0F;
    }
    camera[14] += 6.0F;
    if (orthographic)
    {
        bx::mtxOrtho(input.data(), -10, 10, -10, 10, 0.1F, 100, 0, homogeneous,
                     bx::Handedness::Right);
    }
    else
    {
        bx::mtxProj(input.data(), 75.0F, 1.5F, 0.1F, 100, homogeneous, bx::Handedness::Right);
    }
    constexpr float bias = 0.02F;
    if (!expect(engine::render_detail::prepare_planar_reflection(plane.data(), camera.data(),
                                                                 input.data(), bias, homogeneous,
                                                                 world, view, projection, combined),
                "prepare perspective/orthographic oblique mirror") ||
        !expect(engine::render_detail::linear_determinant(world.data()) < 0,
                "mirror retains negative determinant for winding reversal"))
    {
        return false;
    }
    std::array<float, 16> identity{};
    bx::mtxMul(identity.data(), world.data(), view.data());
    for (std::size_t index = 0; index < 16; ++index)
    {
        if (!expect(std::abs(identity[index] - (index % 5 == 0 ? 1.0F : 0.0F)) < 0.0001F,
                    "mirror view is the actual inverse of reflected world"))
        {
            return false;
        }
    }
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        if (!expect(std::abs(world[12 + axis] - (camera[12 + axis] - 10.0F * normal[axis])) <
                        0.0001F,
                    "camera position is reflected around the transformed plane"))
        {
            return false;
        }
    }
    for (const float height : {-1.0F, bias, 1.0F})
    {
        std::array<float, 3> position{};
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            position[axis] = plane[12 + axis] + normal[axis] * height;
        }
        position[2] -= 5.0F;
        const auto value = clip(combined, position);
        const float near_half_space = value[2] + (homogeneous ? value[3] : 0.0F);
        if (!expect(
                height == bias  ? std::abs(near_half_space) < 0.0001F
                : height > bias ? near_half_space > 0
                                : near_half_space < 0,
                "oblique near plane retains above-water geometry and rejects below-water geometry"))
        {
            return false;
        }
    }
    camera[13] = plane[13] - 10;
    if (!expect(!engine::render_detail::prepare_planar_reflection(
                    plane.data(), camera.data(), input.data(), bias, homogeneous, world, view,
                    projection, combined),
                "back-facing plane does not produce a stale capture"))
    {
        return false;
    }
    camera[13] = std::numeric_limits<float>::quiet_NaN();
    return expect(!engine::render_detail::prepare_planar_reflection(
                      plane.data(), camera.data(), input.data(), bias, homogeneous, world, view,
                      projection, combined),
                  "nonfinite camera is rejected");
}

bool verify_capture_projection(bool homogeneous, bool orthographic, bool off_center)
{
    std::array<float, 16> projection{};
    if (orthographic)
    {
        bx::mtxOrtho(projection.data(), -6, 6, -4, 4, 0.2F, 250, 0, homogeneous,
                     bx::Handedness::Right);
        if (off_center)
        {
            projection[12] = 0.6F;
            projection[13] = -0.3F;
        }
    }
    else
    {
        bx::mtxProj(projection.data(), 70, 1.5F, 0.2F, 250, homogeneous, bx::Handedness::Right);
        if (off_center)
        {
            projection[8] = 0.6F;
            projection[9] = -0.3F;
        }
    }
    engine::CameraSettings lens;
    if (!expect(engine::render_detail::projection_camera_settings(projection.data(), 1.5F,
                                                                  homogeneous, lens),
                "reconstruct the actual capture projection") ||
        !expect(std::abs(lens.near_clip - 0.2F) < 0.0001F && std::abs(lens.far_clip - 250) < 0.1F,
                "capture clipping distances come from its projection, not a fixed camera") ||
        !expect(lens.projection == (orthographic ? engine::schema::Projection::orthographic
                                                 : engine::schema::Projection::perspective),
                "capture preserves orthographic or perspective projection") ||
        !expect(orthographic
                    ? lens.orthographic_size >= 4 - 0.0001F &&
                          (!off_center || lens.orthographic_size > 4)
                    : lens.fov_degrees >= 70 - 0.0001F && (!off_center || lens.fov_degrees > 70),
                "shadow frustum conservatively encloses off-centre captures"))
    {
        return false;
    }
    projection.fill(0);
    return expect(!engine::render_detail::projection_camera_settings(projection.data(), 1.5F,
                                                                     homogeneous, lens),
                  "singular capture projection is rejected");
}
} // namespace

int main()
{
    for (bool homogeneous : {false, true})
    {
        for (bool orthographic : {false, true})
        {
            for (bool tilted : {false, true})
            {
                if (!verify(homogeneous, orthographic, tilted) ||
                    !verify_capture_projection(homogeneous, orthographic, tilted))
                {
                    return 1;
                }
            }
        }
    }
    std::puts("Planar reflection and capture projection math: 16 combinations passed.");
    return 0;
}
