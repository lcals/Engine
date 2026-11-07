#include "runtime/scene/camera.hpp"

#include "runtime/scene/camera_runtime.hpp"

#include "runtime/scene/ecs_storage.hpp"

#include "runtime/scene/camera_math.hpp"
#include "runtime/scene/node.hpp"
#include "runtime/scene/transform.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace engine
{

bool Camera::register_type(ComponentRegistry& registry)
{
    if (!register_component_storage<Camera, Data, Runtime>(registry))
    {
        return false;
    }
    flecs::world(registry.native_world())
        .observer<const ecs_detail::ComponentStorage<Data>>()
        .event(flecs::OnRemove)
        .each(
            [](flecs::entity entity, const auto&)
            {
                const auto metadata = entity.target<ecs_detail::SceneMember>();
                auto* info = metadata ? metadata.try_get_mut<schema::SceneInfoT>() : nullptr;
                const auto* properties = entity.try_get<schema::NodePropertiesT>();
                if (info && properties && info->active_camera_node == properties->id)
                {
                    info->active_camera_node = 0;
                }
            });
    return true;
}

Camera::Runtime* Camera::runtime() const noexcept
{
    return runtime_state<Runtime>();
}

bool Camera::get_settings(Data& output) const noexcept
{
    const auto* value = read_data<Data>();
    output = value ? *value : Data{};
    return value != nullptr;
}

bool Camera::set_settings(const CameraSettings& settings) noexcept
{
    if (!writable())
    {
        return false;
    }
    if (!std::isfinite(settings.fov_degrees) || settings.fov_degrees <= 0.0F ||
        settings.fov_degrees >= 180.0F || !std::isfinite(settings.near_clip) ||
        settings.near_clip <= 0.0F || !std::isfinite(settings.far_clip) ||
        settings.far_clip <= settings.near_clip ||
        (settings.projection != schema::Projection::perspective &&
         settings.projection != schema::Projection::orthographic) ||
        !std::isfinite(settings.orthographic_size) || settings.orthographic_size <= 0.0F)
    {
        return false;
    }
    *write_data<Data>() = settings;
    modified();
    return true;
}

bool Camera::set_hidden_meshes(std::span<const std::uint64_t> mesh_instance_ids)
{
    if (!writable())
    {
        return false;
    }
    if (std::find(mesh_instance_ids.begin(), mesh_instance_ids.end(), 0) != mesh_instance_ids.end())
    {
        return false;
    }
    std::vector<std::uint64_t> candidate(mesh_instance_ids.begin(), mesh_instance_ids.end());
    std::sort(candidate.begin(), candidate.end());
    candidate.erase(std::unique(candidate.begin(), candidate.end()), candidate.end());
    runtime()->hidden_meshes_.swap(candidate);
    return true;
}

bool Camera::screen_point_to_ray(const Vec2& point, const Vec2& viewport_size, Vec3& origin,
                                 Vec3& direction) const noexcept
{
    origin = {};
    direction = {};
    if (!valid())
    {
        return false;
    }
    if (!std::isfinite(viewport_size.x()) || !std::isfinite(viewport_size.y()) ||
        viewport_size.x() <= 0.0 || viewport_size.y() <= 0.0)
    {
        return false;
    }
    return viewport_point_to_ray({point.x() / viewport_size.x(), point.y() / viewport_size.y()},
                                 viewport_size.x() / viewport_size.y(), origin, direction);
}

bool Camera::viewport_point_to_ray(const Vec2& point, double aspect, Vec3& origin,
                                   Vec3& direction) const noexcept
{
    origin = {};
    direction = {};
    if (!valid())
    {
        return false;
    }
    if (!std::isfinite(point.x()) || !std::isfinite(point.y()))
    {
        return false;
    }
    auto transform = node().get_component<Transform>();
    float world[16]{};
    float view[16]{};
    float projection[16]{};
    Data data;
    if (!get_settings(data) || !transform.valid() || !transform.world_matrix(world) ||
        !camera_detail::view_matrix(world, view) ||
        !camera_detail::projection_matrix(data, aspect, false, projection))
    {
        return false;
    }
    const bool perspective = data.projection == schema::Projection::perspective;
    const double near_clip = data.near_clip;
    const double scale = perspective ? near_clip : 1.0;
    // 反解对称投影的 X/Y，直接取近面 Z，避免远近裁剪比影响矩阵求逆精度。
    const std::array<double, 3> local_origin = {(point.x() * 2.0 - 1.0) * scale / projection[0],
                                                (1.0 - point.y() * 2.0) * scale / projection[5],
                                                -near_clip};
    const std::array<double, 3> local_direction =
        perspective ? local_origin : std::array<double, 3>{0.0, 0.0, -1.0};
    std::array<double, 3> world_origin{};
    std::array<double, 3> world_direction{};
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        // view 的线性部分是正交相机基的转置，不把节点缩放重新带入射线。
        world_origin[axis] = world[12 + axis];
        for (std::size_t component = 0; component < 3; ++component)
        {
            world_origin[axis] += view[axis * 4 + component] * local_origin[component];
            world_direction[axis] += view[axis * 4 + component] * local_direction[component];
        }
        if (!std::isfinite(world_origin[axis]) || !std::isfinite(world_direction[axis]))
        {
            return false;
        }
    }
    const double length = std::hypot(world_direction[0], world_direction[1], world_direction[2]);
    if (!std::isfinite(length) || !(length > 0.0))
    {
        return false;
    }
    origin = {world_origin[0], world_origin[1], world_origin[2]};
    direction = {world_direction[0] / length, world_direction[1] / length,
                 world_direction[2] / length};
    return true;
}

void Camera::clear_hidden_meshes() noexcept
{
    if (!writable())
    {
        return;
    }
    runtime()->hidden_meshes_.clear();
}

bool Camera::mesh_visible(std::uint64_t mesh_instance_id) const noexcept
{
    if (!valid())
    {
        return false;
    }
    return mesh_instance_id != 0 &&
           !std::binary_search(runtime()->hidden_meshes_.begin(), runtime()->hidden_meshes_.end(),
                               mesh_instance_id);
}

bool Camera::restore_data(const schema::ComponentDataT& data)
{
    if (!data.flags)
    {
        return false;
    }
    if (!writable())
    {
        return false;
    }
    const auto* settings = data.payload.AsCameraData();
    if (settings == nullptr || !set_settings(*settings))
    {
        return false;
    }
    if (!restore_flags(data))
    {
        return false;
    }
    clear_hidden_meshes();
    return true;
}

} // namespace engine
