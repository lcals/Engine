#include "runtime/scene/collider.hpp"

#include "runtime/core/guid_map.hpp"
#include "runtime/physics/mesh_collision_data.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/node.hpp"
#include "runtime/scene/rigid_body.hpp"

#include <atomic>
#include <cmath>
#include <limits>
#include <utility>

namespace engine
{
namespace
{

std::uint64_t next_instance_id() noexcept
{
    static std::atomic<std::uint64_t> sequence{0};
    auto current = sequence.load(std::memory_order_relaxed);
    while (current != std::numeric_limits<std::uint64_t>::max())
    {
        if (sequence.compare_exchange_weak(current, current + 1, std::memory_order_relaxed))
        {
            return current + 1;
        }
    }
    return 0;
}

bool bounded(double value, double minimum, double maximum) noexcept
{
    return std::isfinite(value) && value >= minimum && value <= maximum;
}

} // namespace

Collider::Collider(Node& node) : Component(node), instance_id_(next_instance_id())
{
    mutable_data().payload.Set(schema::ColliderDataT{});
}

bool Collider::initialize()
{
    return instance_id_ != 0;
}

std::uint64_t Collider::instance_id() const noexcept
{
    return instance_id_;
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

const schema::ColliderDataT& Collider::settings() const noexcept
{
    return *data().payload.AsColliderData();
}

bool Collider::set_settings(const schema::ColliderDataT& value)
{
    if (!valid_settings(value, transient_mesh_.get(), mesh_parts_.get()))
    {
        return false;
    }
    auto replacement = mesh_;
    auto transient = transient_mesh_;
    auto parts = mesh_parts_;
    if (value.shape == schema::ColliderShape::mesh)
    {
        const auto* body = node().get_component<RigidBody>();
        if (body != nullptr && body->settings().motion != schema::BodyMotion::static_body)
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
    *mutable_data().payload.AsColliderData() = value;
    mesh_ = std::move(replacement);
    transient_mesh_ = std::move(transient);
    mesh_parts_ = std::move(parts);
    return true;
}

bool Collider::restore_data(const schema::ComponentDataT& value)
{
    const auto* settings = value.payload.AsColliderData();
    if (settings == nullptr || !valid_settings(*settings) || !set_settings(*settings))
    {
        return false;
    }
    set_enabled(value.enabled);
    return true;
}

bool Collider::build_data(schema::ComponentDataT& output) const
{
    output = {};
    return transient_mesh_ == nullptr && mesh_parts_ == nullptr && Component::build_data(output);
}

const schema::MeshDataT* Collider::mesh_data() const noexcept
{
    return transient_mesh_ ? transient_mesh_.get() : mesh_ ? mesh_->mesh() : nullptr;
}

bool Collider::set_mesh(std::string_view path)
{
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
    return mesh_ ? std::string_view(mesh_->info().path) : std::string_view{};
}

} // namespace engine
