#include "plugins/runtime/destruction/destruction_component.hpp"

#include "plugins/runtime/destruction/destruction_system.hpp"
#include "runtime/core/guid_map.hpp"
#include "runtime/resource/resource_system.hpp"

#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/verifier.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace engine::plugins::destruction
{

DestructionComponent::DestructionComponent(Node& node) : Component(node)
{
    mutable_data().payload.Set(schema::DestructionDataT{});
}

bool DestructionComponent::valid_settings(const schema::DestructionDataT& value) noexcept
{
    return (value.destruction_guid.empty() || GuidMap::valid_guid(value.destruction_guid)) &&
           (value.material_guid.empty() || GuidMap::valid_guid(value.material_guid)) &&
           std::isfinite(value.density) && value.density > 0 && value.density <= 100000 &&
           std::isfinite(value.friction) && value.friction >= 0 && value.friction <= 1 &&
           std::isfinite(value.restitution) && value.restitution >= 0 && value.restitution <= 1;
}

const schema::DestructionDataT& DestructionComponent::settings() const noexcept
{
    return *data().payload.AsDestructionData();
}

bool DestructionComponent::set_settings(const schema::DestructionDataT& value)
{
    if (!valid_settings(value))
    {
        last_error_ = "Invalid destruction component settings";
        return false;
    }
    auto* resources = ResourceSystem::instance();
    ResourcePtr replacement;
    DestructionGraph candidate;
    if (!value.destruction_guid.empty())
    {
        if (!resources || !resources->loader().load(value.destruction_guid, replacement) ||
            !replacement || replacement->info().kind != schema::AssetKind::destruction)
        {
            last_error_ = "Cannot load destruction resource";
            return false;
        }
        const auto bytes = replacement->bytes();
        const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.data());
        flatbuffers::Verifier verifier(data, bytes.size());
        if (bytes.size() > 64U * 1024U * 1024U ||
            !schema::VerifyDestructionAssetDataBuffer(verifier) ||
            !candidate.initialize(*schema::UnPackDestructionAssetData(data), last_error_))
        {
            last_error_ = "Invalid destruction asset: " + last_error_;
            return false;
        }
        if (!value.material_guid.empty())
        {
            ResourcePtr material;
            if (!resources->loader().load(value.material_guid, material) || !material ||
                !material->material())
            {
                last_error_ = "Cannot load destruction material";
                return false;
            }
        }
    }
    const bool retain = resource_ && replacement &&
                        resource_->info().guid == replacement->info().guid &&
                        resource_->info().content_hash == replacement->info().content_hash;
    if (retain && !graph_.clone(candidate, last_error_))
    {
        return false;
    }
    const auto previous_settings = settings();
    auto previous_resource = resource_;
    *mutable_data().payload.AsDestructionData() = value;
    resource_ = std::move(replacement);
    if (ready_ && resource_)
    {
        auto* system = DestructionSystem::instance();
        if (!system || !system->rebuild(*this, std::move(candidate), retain))
        {
            *mutable_data().payload.AsDestructionData() = previous_settings;
            resource_ = std::move(previous_resource);
            return false;
        }
    }
    else
    {
        graph_ = std::move(candidate);
        ready_ = false;
        pending_ = resource_ != nullptr;
    }
    last_error_.clear();
    return true;
}

bool DestructionComponent::restore_data(const schema::ComponentDataT& data)
{
    const auto* value = data.payload.AsDestructionData();
    if (!value || !set_settings(*value))
    {
        return false;
    }
    set_enabled(data.enabled);
    set_visible(data.visible);
    return true;
}

bool DestructionComponent::damage_sphere(const Vec3& center, float radius, float damage,
                                         float impulse)
{
    auto* system = DestructionSystem::instance();
    if (!system || !ready_ || !active())
    {
        last_error_ = "Destruction component is not ready";
        return false;
    }
    return system->damage(*this, center, radius, damage, impulse);
}

bool DestructionComponent::reset()
{
    auto* system = DestructionSystem::instance();
    return system && resource_ && system->reset(*this);
}

bool DestructionComponent::refresh_support(const Collider& support, float max_gap,
                                           float min_coverage)
{
    auto* system = DestructionSystem::instance();
    if (!system)
    {
        last_error_ = "Destruction system is not ready";
        return false;
    }
    return system->refresh_support(*this, support, max_gap, min_coverage);
}

bool DestructionComponent::apply_stress(const Vec3& gravity, float elastic_limit, float fatal_limit,
                                         std::uint32_t iterations, std::uint32_t& broken_bonds)
{
    broken_bonds = 0;
    auto* system = DestructionSystem::instance();
    if (!system)
    {
        last_error_ = "Destruction system is not ready";
        return false;
    }
    return system->apply_stress(*this, gravity, elastic_limit, fatal_limit, iterations,
                                broken_bonds);
}

bool DestructionComponent::owns_fragment(std::uint64_t node_id) const noexcept
{
    auto* system = DestructionSystem::instance();
    return system && system->owns_fragment(*this, node_id);
}

bool DestructionComponent::retire_fragments(std::uint32_t max_count, double max_age_seconds,
                                            std::uint32_t& retired_count)
{
    retired_count = 0;
    auto* system = DestructionSystem::instance();
    if (!system)
    {
        last_error_ = "Destruction system is not ready";
        return false;
    }
    return system->retire_fragments(*this, max_count, max_age_seconds, retired_count);
}

bool DestructionComponent::snapshot(ByteBuffer& output) const
{
    output.clear();
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
    auto* system = DestructionSystem::instance();
    const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.data());
    flatbuffers::Verifier verifier(data, bytes.size());
    if (!system || !resource_ || bytes.size() > 4U * 1024U * 1024U ||
        !schema::VerifyDestructionStateDataBuffer(verifier))
    {
        last_error_ = "Invalid destruction state";
        return false;
    }
    return system->restore(*this, *schema::UnPackDestructionStateData(data));
}

bool DestructionComponent::ready() const noexcept
{
    return ready_;
}
std::uint64_t DestructionComponent::revision() const noexcept
{
    return revision_;
}
std::size_t DestructionComponent::chunk_count() const noexcept
{
    return graph_.data().chunks.size();
}
std::size_t DestructionComponent::group_count() const noexcept
{
    return graph_.groups().size();
}
std::size_t DestructionComponent::dynamic_groups() const noexcept
{
    return static_cast<std::size_t>(std::count_if(graph_.groups().begin(), graph_.groups().end(),
                                                  [](const auto& group)
                                                  { return !group.anchored; }));
}
const std::string& DestructionComponent::last_error() const noexcept
{
    return last_error_;
}

} // namespace engine::plugins::destruction
