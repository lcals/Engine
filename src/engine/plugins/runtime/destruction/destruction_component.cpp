#include "plugins/runtime/destruction/destruction_component.hpp"

#include "plugins/runtime/destruction/destruction_component_runtime.hpp"

#include "runtime/scene/collider.hpp"
#include "runtime/scene/ecs_storage.hpp"

#include "plugins/runtime/destruction/destruction_resource.hpp"
#include "plugins/runtime/destruction/destruction_system.hpp"
#include "runtime/core/guid_map.hpp"
#include "runtime/resource/types/material_resource.hpp"
#include "runtime/resource/virtual_file_system.hpp"
#include "runtime/scene/resource_binding.hpp"

#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/verifier.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace engine::plugins::destruction
{

bool DestructionComponent::register_type(ComponentRegistry& registry)
{
    return register_component_storage<DestructionComponent, Data, Runtime>(registry);
}

DestructionComponent::Runtime* DestructionComponent::runtime() const noexcept
{
    return runtime_state<Runtime>();
}

bool DestructionComponent::valid_settings(const schema::DestructionDataT& value) noexcept
{
    return (value.destruction_guid.empty() || GuidMap::valid_guid(value.destruction_guid)) &&
           (value.material_guid.empty() || GuidMap::valid_guid(value.material_guid)) &&
           std::isfinite(value.density) && value.density > 0 && value.density <= 100000 &&
           std::isfinite(value.friction) && value.friction >= 0 && value.friction <= 1 &&
           std::isfinite(value.restitution) && value.restitution >= 0 && value.restitution <= 1;
}

schema::DestructionDataT DestructionComponent::settings() const noexcept
{
    const auto* value = read_data<Data>();
    return value ? *value : Data{};
}

bool DestructionComponent::get_settings(Data& output) const noexcept
{
    const auto* value = read_data<Data>();
    output = value ? *value : Data{};
    return value != nullptr;
}

bool DestructionComponent::set_settings(const schema::DestructionDataT& value)
{
    if (!writable())
    {
        return false;
    }
    if (!valid_settings(value))
    {
        runtime()->last_error_ = "Invalid destruction component settings";
        return false;
    }
    auto* resources = node().world()->resource_loader();
    resource_detail::ResourceReference replacement;
    resource_detail::ResourceReference material;
    DestructionGraph candidate;
    if (!value.destruction_guid.empty())
    {
        if (!resources ||
            !ecs_detail::acquire_resource(node(), value.destruction_guid, replacement) ||
            !replacement || replacement->info().kind != schema::AssetKind::destruction)
        {
            runtime()->last_error_ = "Cannot load destruction resource";
            return false;
        }
        if (!replacement.data<DestructionResource>() ||
            !candidate.initialize(*replacement.data<DestructionResource>(), runtime()->last_error_))
        {
            runtime()->last_error_ = "Invalid destruction asset: " + runtime()->last_error_;
            return false;
        }
        const auto material_path = value.material_guid.empty()
                                       ? std::string_view("builtin://materials/default-pbr")
                                       : std::string_view(value.material_guid);
        if (!ecs_detail::acquire_resource(node(), material_path, material) ||
            !material.data<MaterialResource>())
        {
            runtime()->last_error_ = "Material must be loaded before binding";
            return false;
        }
    }
    const bool retain =
        runtime()->resource_ && replacement &&
        runtime()->resource_->info().guid == replacement->info().guid &&
        runtime()->resource_->info().content_hash == replacement->info().content_hash;
    if (retain && !runtime()->graph_.clone(candidate, runtime()->last_error_))
    {
        return false;
    }
    const auto previous_settings = settings();
    auto previous_resource = std::move(runtime()->resource_);
    auto previous_material = std::move(runtime()->material_);
    *write_data<Data>() = value;
    modified();
    runtime()->resource_ = std::move(replacement);
    runtime()->material_ = std::move(material);
    if (runtime()->ready_ && runtime()->resource_)
    {
        auto* system = DestructionSystem::instance();
        if (!system || !system->rebuild(*this, std::move(candidate), retain))
        {
            *write_data<Data>() = previous_settings;
            modified();
            runtime()->resource_ = std::move(previous_resource);
            runtime()->material_ = std::move(previous_material);
            return false;
        }
    }
    else
    {
        runtime()->graph_ = std::move(candidate);
        runtime()->ready_ = false;
        runtime()->pending_ = static_cast<bool>(runtime()->resource_);
    }
    runtime()->last_error_.clear();
    return true;
}

bool DestructionComponent::restore_data(const schema::ComponentDataT& data)
{
    if (!data.flags)
    {
        return false;
    }
    if (!writable())
    {
        return false;
    }
    const auto* value = data.payload.AsDestructionData();
    if (!value || !set_settings(*value))
    {
        return false;
    }
    if (!restore_flags(data))
    {
        return false;
    }
    return true;
}

bool DestructionComponent::damage_sphere(const Vec3& center, float radius, float damage,
                                         float impulse)
{
    if (!writable())
    {
        return false;
    }
    auto* system = DestructionSystem::instance();
    if (!system || !runtime()->ready_ || !active())
    {
        runtime()->last_error_ = "Destruction component is not ready";
        return false;
    }
    return system->damage(*this, center, radius, damage, impulse);
}

bool DestructionComponent::reset()
{
    if (!writable())
    {
        return false;
    }
    auto* system = DestructionSystem::instance();
    return system && runtime()->resource_ && system->reset(*this);
}

bool DestructionComponent::refresh_support(const Collider& support, float max_gap,
                                           float min_coverage)
{
    if (!writable())
    {
        return false;
    }
    auto* system = DestructionSystem::instance();
    if (!system)
    {
        runtime()->last_error_ = "Destruction system is not ready";
        return false;
    }
    return system->refresh_support(*this, support, max_gap, min_coverage);
}

bool DestructionComponent::apply_stress(const Vec3& gravity, float elastic_limit, float fatal_limit,
                                        std::uint32_t iterations, std::uint32_t& broken_bonds)
{
    broken_bonds = 0;
    if (!writable())
    {
        return false;
    }
    auto* system = DestructionSystem::instance();
    if (!system)
    {
        runtime()->last_error_ = "Destruction system is not ready";
        return false;
    }
    return system->apply_stress(*this, gravity, elastic_limit, fatal_limit, iterations,
                                broken_bonds);
}

bool DestructionComponent::owns_fragment(std::uint64_t node_id) const noexcept
{
    if (!valid())
    {
        return false;
    }
    auto* system = DestructionSystem::instance();
    return system && system->owns_fragment(*this, node_id);
}

bool DestructionComponent::retire_fragments(std::uint32_t max_count, double max_age_seconds,
                                            std::uint32_t& retired_count)
{
    retired_count = 0;
    if (!writable())
    {
        return false;
    }
    auto* system = DestructionSystem::instance();
    if (!system)
    {
        runtime()->last_error_ = "Destruction system is not ready";
        return false;
    }
    return system->retire_fragments(*this, max_count, max_age_seconds, retired_count);
}

bool DestructionComponent::snapshot(ByteBuffer& output) const
{
    output.clear();
    if (!valid())
    {
        return false;
    }
    auto* system = DestructionSystem::instance();
    schema::DestructionStateDataT state;
    if (!system || !system->snapshot(*this, state))
    {
        return false;
    }
    flatbuffers::FlatBufferBuilder builder;
    schema::FinishDestructionStateDataBuffer(builder,
                                             schema::DestructionStateData::Pack(builder, &state));
    const auto* begin = reinterpret_cast<const std::byte*>(builder.GetBufferPointer());
    output.assign(begin, begin + builder.GetSize());
    return true;
}

bool DestructionComponent::restore(ByteView bytes)
{
    if (!writable())
    {
        return false;
    }
    auto* system = DestructionSystem::instance();
    const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.data());
    flatbuffers::Verifier verifier(data, bytes.size());
    if (!system || !runtime()->resource_ || bytes.size() > 4U * 1024U * 1024U ||
        !schema::VerifyDestructionStateDataBuffer(verifier))
    {
        runtime()->last_error_ = "Invalid destruction state";
        return false;
    }
    return system->restore(*this, *schema::UnPackDestructionStateData(data));
}

bool DestructionComponent::ready() const noexcept
{
    if (!valid())
    {
        return false;
    }
    return runtime()->ready_;
}
std::uint64_t DestructionComponent::revision() const noexcept
{
    if (!valid())
    {
        return {};
    }
    return runtime()->revision_;
}
std::size_t DestructionComponent::chunk_count() const noexcept
{
    if (!valid())
    {
        return {};
    }
    return runtime()->graph_.data().chunks.size();
}
std::size_t DestructionComponent::group_count() const noexcept
{
    if (!valid())
    {
        return {};
    }
    return runtime()->graph_.groups().size();
}
std::size_t DestructionComponent::dynamic_groups() const noexcept
{
    if (!valid())
    {
        return {};
    }
    return static_cast<std::size_t>(
        std::count_if(runtime()->graph_.groups().begin(), runtime()->graph_.groups().end(),
                      [](const auto& group) { return !group.anchored; }));
}
std::string DestructionComponent::last_error() const noexcept
{
    if (!valid())
    {
        return {};
    }
    return runtime()->last_error_;
}

} // namespace engine::plugins::destruction
