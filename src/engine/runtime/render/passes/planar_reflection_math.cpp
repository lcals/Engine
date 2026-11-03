#include "runtime/render/passes/planar_reflection_math.hpp"
#include "runtime/render/render_math.hpp"
#include "runtime/scene/camera_math.hpp"
#include <algorithm>
#include <bx/math.h>
#include <cmath>
namespace engine::render_detail
{
bool prepare_planar_reflection(const float* plane_world, const float* camera_world,
                               const float* camera_projection, float clip_bias, bool homogeneous,
                               std::array<float, 16>& world, std::array<float, 16>& view,
                               std::array<float, 16>& projection,
                               std::array<float, 16>& view_projection)
{
    if (plane_world == nullptr || camera_world == nullptr || camera_projection == nullptr ||
        !finite_matrix(plane_world) || !finite_matrix(camera_world) ||
        !finite_matrix(camera_projection) || !std::isfinite(clip_bias) || clip_bias < 0.0F ||
        clip_bias > 1.0F)
    {
        return false;
    }
    // Reflector.js (Three.js bf8f297466d0a8f8ef141d83113c79fadbe1019c, MIT):
    // mirror the camera and use Lengyel's oblique near plane. The full reflection
    // matrix retains its negative determinant; the draw submission reverses winding.
    // Source/license: third_party/notices/threejs-reflections.md.
    float normals[16];
    if (!normal_matrix(plane_world, normals))
    {
        return false;
    }
    std::array<float, 3> n{normals[4], normals[5], normals[6]};
    const float length = std::hypot(n[0], n[1], n[2]);
    if (!(length > 0.0F))
    {
        return false;
    }
    for (auto& value : n)
    {
        value /= length;
    }
    float distance = 0;
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        distance += n[axis] * (camera_world[12 + axis] - plane_world[12 + axis]);
    }
    if (distance <= clip_bias)
    {
        return false;
    }
    float reflection[16];
    bx::mtxIdentity(reflection);
    float d = 0;
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        d -= n[axis] * plane_world[12 + axis];
    }
    for (std::size_t row = 0; row < 3; ++row)
    {
        for (std::size_t col = 0; col < 3; ++col)
        {
            reflection[col * 4 + row] -= 2 * n[col] * n[row];
        }
        reflection[12 + row] = -2 * d * n[row];
    }
    float camera_view[16]{};
    float normalized_camera_world[16]{};
    if (!camera_detail::view_matrix(camera_world, camera_view))
    {
        return false;
    }
    bx::mtxInverse(normalized_camera_world, camera_view);
    bx::mtxMul(world.data(), normalized_camera_world, reflection);
    // Rebuilding a look-at basis here would remove the reflection's negative determinant.
    bx::mtxInverse(view.data(), world.data());
    if (!finite_matrix(view.data()))
    {
        return false;
    }
    std::copy_n(camera_projection, 16, projection.begin());
    const std::array<float, 4> plane{n[0], n[1], n[2], d - clip_bias};
    std::array<float, 4> camera_plane{};
    for (std::size_t col = 0; col < 4; ++col)
    {
        for (std::size_t row = 0; row < 4; ++row)
        {
            camera_plane[col] += world[col * 4 + row] * plane[row];
        }
    }
    float inverse[16];
    bx::mtxInverse(inverse, projection.data());
    const std::array<float, 4> corner{camera_plane[0] < 0 ? -1.0F : 1.0F,
                                      camera_plane[1] < 0 ? -1.0F : 1.0F, 1, 1};
    std::array<float, 4> q{};
    float dot = 0;
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t col = 0; col < 4; ++col)
        {
            q[row] += inverse[col * 4 + row] * corner[col];
        }
        dot += camera_plane[row] * q[row];
    }
    if (!std::isfinite(dot) || std::abs(dot) < 0.000001F)
    {
        return false;
    }
    const float factor = (homogeneous ? 2.0F : 1.0F) / dot;
    for (std::size_t col = 0; col < 4; ++col)
    {
        projection[col * 4 + 2] =
            camera_plane[col] * factor - (homogeneous ? projection[col * 4 + 3] : 0.0F);
    }
    bx::mtxMul(view_projection.data(), view.data(), projection.data());
    return finite_matrix(projection.data()) && finite_matrix(view_projection.data());
}
} // namespace engine::render_detail
