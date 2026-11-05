#include "runtime/scene/collider.hpp"

#include "runtime/scene/collider_runtime.hpp"

#include "runtime/scene/ecs_storage.hpp"

#include "runtime/core/guid_map.hpp"
#include "runtime/physics/mesh_collision_data.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/node.hpp"
#include "runtime/scene/rigid_body.hpp"

#include <cmath>
#include <utility>

namespace engine
{
namespace
{

bool bounded(double value, double minimum, double maximum) noexcept
{
    return std::isfinite(value) && value >= minimum && value <= maximum;
}

} // namespace

bool Collider::register_type(ComponentRegistry& registry)
{
    return register_component_storage<Collider, Data, Runtime>(registry);
}

Collider::Runtime* Collider::runtime() const noexcept
{
    return runtime_state<Runtime>();
}

bool Collider::valid_settings(const schema::ColliderDataT& value,
                              const schema::MeshDataT* transient_mesh,
                              const MeshCollisionData* mesh_parts) noexcept
{
    constexpr std::size_t maximum_hulls = 4096;
    constexpr std::size_t maximum_vertices = 1024U * 1024U;
    if (value.shape == schema::ColliderShape::compound)
    {
        if (value.hulls.empty() || value.hulls.size() > maximum_hulls)
        {
            return false;
        }
        std::size_t vertex_count = 0;
        for (const auto& hull : value.hulls)
        {
            if (!hull || hull->vertices.size() < 4 ||
                hull->vertices.size() > maximum_vertices - vertex_count)
            {
                return false;
            }
            vertex_count += hull->vertices.size();
            for (const auto& vertex : hull->vertices)
            {
                if (!bounded(vertex.x(), -100000.0, 100000.0) ||
                    !bounded(vertex.y(), -100000.0, 100000.0) ||
                    !bounded(vertex.z(), -100000.0, 100000.0))
                {
                    return false;
                }
            }
        }
    }
    else if (!value.hulls.empty())
    {
        return false;
    }
    return value.shape >= schema::ColliderShape::box &&
           value.shape <= schema::ColliderShape::compound &&
           (value.shape == schema::ColliderShape::mesh
                ? GuidMap::valid_guid(value.mesh_guid) ||
                      (value.mesh_guid.empty() &&
                       (transient_mesh != nullptr || mesh_parts != nullptr))
                : value.mesh_guid.empty()) &&
           bounded(value.half_extents.x(), 0.0001, 100000.0) &&
           bounded(value.half_extents.y(), 0.0001, 100000.0) &&
           bounded(value.half_extents.z(), 0.0001, 100000.0) &&
           bounded(value.radius, 0.0001F, 100000.0) && bounded(value.half_height, 0.0, 100000.0) &&
           bounded(value.center.x(), -100000.0, 100000.0) &&
           bounded(value.center.y(), -100000.0, 100000.0) &&
           bounded(value.center.z(), -100000.0, 100000.0) && bounded(value.friction, 0.0, 1.0) &&
           bounded(value.restitution, 0.0, 1.0);
}

schema::ColliderDataT Collider::settings() const noexcept
{
    const auto* value = read_data<Data>();
    return value ? *value : Data{};
}

bool Collider::get_settings(Data& output) const noexcept
{
    const auto* value = read_data<Data>();
    output = value ? *value : Data{};
    return value != nullptr;
}

bool Collider::set_settings(const schema::ColliderDataT& value)
{
    if (!writable())
    {
        return false;
    }
    if (!valid_settings(value, runtime()->transient_mesh_.get(), runtime()->mesh_parts_.get()))
    {
        return false;
    }
    auto replacement = runtime()->mesh_;
    auto transient = runtime()->transient_mesh_;
    auto parts = runtime()->mesh_parts_;
    if (value.shape == schema::ColliderShape::mesh)
    {
        auto body = node().get_component<RigidBody>();
        if (body.valid() && body.settings().motion != schema::BodyMotion::static_body)
        {
            return false;
        }
        if (value.mesh_guid.empty())
        {
            replacement.reset();
        }
        else if (!replacement || value.mesh_guid != settings().mesh_guid)
        {
            auto* resources = ResourceSystem::instance();
            if (resources == nullptr || !resources->loader().load(value.mesh_guid, replacement) ||
                !replacement || replacement->mesh() == nullptr)
            {
                return false;
            }
        }
        if (!value.mesh_guid.empty())
        {
            transient.reset();
            parts.reset();
        }
    }
    else
    {
        replacement.reset();
        transient.reset();
        parts.reset();
    }
    *write_data<Data>() = value;
    modified();
    runtime()->mesh_ = std::move(replacement);
    runtime()->transient_mesh_ = std::move(transient);
    runtime()->mesh_parts_ = std::move(parts);
    return true;
}

bool Collider::restore_data(const schema::ComponentDataT& value)
{
    if (!value.flags)
    {
        return false;
    }
    if (!writable())
    {
        return false;
    }
    const auto* settings = value.payload.AsColliderData();
    if (settings == nullptr || !valid_settings(*settings) || !set_settings(*settings))
    {
        return false;
    }
    if (!restore_flags(value))
    {
        return false;
    }
    return true;
}

bool Collider::build_data(schema::ComponentDataT& output) const
{
    output = {};
    if (!valid())
    {
        return false;
    }
    return runtime()->transient_mesh_ == nullptr && runtime()->mesh_parts_ == nullptr &&
           build_storage_data(output);
}

const schema::MeshDataT* Collider::mesh_data() const noexcept
{
    if (!valid())
    {
        return {};
    }
    return runtime()->transient_mesh_ ? runtime()->transient_mesh_.get()
           : runtime()->mesh_         ? runtime()->mesh_->mesh()
                                      : nullptr;
}

bool Collider::set_mesh(std::string_view path)
{
    if (!writable())
    {
        return false;
    }
    auto replacement = settings();
    if (!ResourceSystem::path_to_guid(path, replacement.mesh_guid))
    {
        return false;
    }
    replacement.shape = schema::ColliderShape::mesh;
    replacement.hulls.clear();
    return set_settings(replacement);
}

std::string_view Collider::mesh_path() const noexcept
{
    if (!valid())
    {
        return {};
    }
    return runtime()->mesh_ ? std::string_view(runtime()->mesh_->info().path) : std::string_view{};
}

} // namespace engine
