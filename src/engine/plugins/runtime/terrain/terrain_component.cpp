#include "plugins/runtime/terrain/terrain_component.hpp"

#include "plugins/runtime/terrain/terrain_component_runtime.hpp"

#include "runtime/scene/ecs_storage.hpp"

#include "plugins/runtime/terrain/terrain_resource.hpp"
#include "plugins/runtime/terrain/terrain_system.hpp"
#include "runtime/core/guid_map.hpp"
#include "runtime/performance/profiling.hpp"
#include "runtime/resource/types/material_resource.hpp"
#include "runtime/resource/virtual_file_system.hpp"
#include "runtime/scene/resource_binding.hpp"
#include "runtime/schema/terrain_state_generated.h"
#include "runtime/utils/path_utils.hpp"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/verifier.h>

#include <cmath>
#include <utility>

namespace engine::plugins::terrain
{
namespace
{
bool color_valid(const schema::Color& color)
{
    for (float value : {color.r(), color.g(), color.b(), color.a()})
    {
        if (!std::isfinite(value) || value < 0 || value > 1)
        {
            return false;
        }
    }
    return true;
}

bool same_dimensions(const schema::TerrainAssetDataT& a, const schema::TerrainAssetDataT& b)
{
    return a.cells_x == b.cells_x && a.cells_y == b.cells_y && a.cells_z == b.cells_z &&
           a.voxel_size == b.voxel_size && a.chunk_cells == b.chunk_cells &&
           a.origin.x() == b.origin.x() && a.origin.y() == b.origin.y() &&
           a.origin.z() == b.origin.z();
}
} // namespace

bool TerrainComponent::register_type(ComponentRegistry& registry)
{
    return register_component_storage<TerrainComponent, Data, Runtime>(registry);
}

TerrainComponent::Runtime* TerrainComponent::runtime() const noexcept
{
    return runtime_state<Runtime>();
}

bool TerrainComponent::valid_settings(const schema::TerrainDataT& settings) noexcept
{
    return (settings.terrain_guid.empty() || GuidMap::valid_guid(settings.terrain_guid)) &&
           (settings.material_guid.empty() || GuidMap::valid_guid(settings.material_guid)) &&
           color_valid(settings.surface_color) && color_valid(settings.wall_color) &&
           std::isfinite(settings.uv_scale) && settings.uv_scale > 0 && settings.uv_scale <= 100;
}

schema::TerrainDataT TerrainComponent::settings() const noexcept
{
    Data output{};
    (void)get_settings(output);
    return output;
}

bool TerrainComponent::get_settings(Data& output) const noexcept
{
    const auto* value = read_data<Data>();
    output = value ? *value : Data{};
    return value != nullptr;
}

bool TerrainComponent::set_settings(const schema::TerrainDataT& value)
{
    if (!writable())
    {
        return false;
    }
    if (!valid_settings(value))
    {
        runtime()->last_error_ = "Invalid terrain component settings";
        return false;
    }
    auto* resources = node().world()->resource_loader();
    resource_detail::ResourceReference replacement;
    resource_detail::ResourceReference material;
    TerrainVolume candidate;
    if (!value.terrain_guid.empty())
    {
        if (!resources || !ecs_detail::acquire_resource(node(), value.terrain_guid, replacement) ||
            !replacement || replacement->info().kind != schema::AssetKind::terrain ||
            !replacement.data<TerrainResource>() ||
            !candidate.initialize(*replacement.data<TerrainResource>(), runtime()->last_error_))
        {
            runtime()->last_error_ = "Cannot load terrain resource";
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
    if (retain)
    {
        if (runtime()->detached_ && value != settings())
        {
            runtime()->last_error_ =
                "Reset terrain fragments before changing their material or settings";
            return false;
        }
        candidate = runtime()->volume_;
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
        auto* system = TerrainSystem::instance();
        if (!system || !system->commit(*this, std::move(candidate)))
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
        runtime()->volume_ = std::move(candidate);
        runtime()->triangles_ = 0;
        runtime()->ready_ = false;
        runtime()->pending_ = static_cast<bool>(runtime()->resource_);
    }
    if (!retain)
    {
        if (auto* system = TerrainSystem::instance())
        {
            system->clear_fragments(*this);
        }
        runtime()->detached_ = false;
    }
    runtime()->last_error_.clear();
    return true;
}

bool TerrainComponent::restore_data(const schema::ComponentDataT& data)
{
    if (!data.flags)
    {
        return false;
    }
    if (!writable())
    {
        return false;
    }
    const auto* value = data.payload.AsTerrainData();
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

bool TerrainComponent::apply_sphere(const Vec3& center, float radius, bool fill)
{
    if (!writable())
    {
        return false;
    }
    ENGINE_PROFILE_ZONE("Terrain.brush");
    auto* system = TerrainSystem::instance();
    if (!runtime()->ready_ || !system || !active())
    {
        runtime()->last_error_ = "Terrain is not ready";
        return false;
    }
    auto candidate = runtime()->volume_;
    return candidate.apply_sphere(center, radius, fill, runtime()->last_error_) &&
           system->commit(*this, std::move(candidate));
}

bool TerrainComponent::retire_fragments(std::uint32_t max_count, double max_age_seconds,
                                        std::uint32_t& retired_count)
{
    retired_count = 0;
    if (!writable())
    {
        return false;
    }
    auto* system = TerrainSystem::instance();
    if (!system)
    {
        runtime()->last_error_ = "Terrain system is not ready";
        return false;
    }
    return system->retire_fragments(*this, max_count, max_age_seconds, retired_count);
}

bool TerrainComponent::reset()
{
    if (!writable())
    {
        return false;
    }
    auto* system = TerrainSystem::instance();
    schema::TerrainAssetDataT data;
    TerrainVolume candidate;
    if (!runtime()->resource_ || !system || !runtime()->resource_.data<TerrainResource>() ||
        !candidate.initialize(*runtime()->resource_.data<TerrainResource>(),
                              runtime()->last_error_))
    {
        return false;
    }
    if (!system->commit(*this, std::move(candidate)))
    {
        return false;
    }
    system->clear_fragments(*this);
    runtime()->detached_ = false;
    return true;
}

bool TerrainComponent::collapse_detached(std::uint32_t min_samples, std::uint32_t max_fragments,
                                         std::uint32_t fragment_cells, float density)
{
    if (!writable())
    {
        return false;
    }
    auto* system = TerrainSystem::instance();
    if (!system)
    {
        runtime()->last_error_ = "Terrain system is not available";
        return false;
    }
    return system->collapse_detached(*this, min_samples, max_fragments, fragment_cells, density);
}

std::size_t TerrainComponent::fragment_count() const noexcept
{
    if (!valid())
    {
        return {};
    }
    const auto* system = TerrainSystem::instance();
    return system ? system->fragment_count(*this) : 0;
}

bool TerrainComponent::snapshot(ByteBuffer& output) const
{
    output.clear();
    if (!valid())
    {
        return false;
    }
    if (!runtime()->ready_ || !runtime()->resource_ || runtime()->detached_)
    {
        return false;
    }
    schema::TerrainStateDataT state;
    state.base_guid = runtime()->resource_->info().guid;
    state.base_hash = runtime()->resource_->info().content_hash;
    state.volume = std::make_unique<schema::TerrainAssetDataT>(runtime()->volume_.data());
    flatbuffers::FlatBufferBuilder builder;
    schema::FinishTerrainStateDataBuffer(builder, schema::TerrainStateData::Pack(builder, &state));
    const auto* begin = reinterpret_cast<const std::byte*>(builder.GetBufferPointer());
    output.assign(begin, begin + builder.GetSize());
    return true;
}

bool TerrainComponent::restore(ByteView bytes)
{
    if (!writable())
    {
        return false;
    }
    if (runtime()->detached_)
    {
        runtime()->last_error_ =
            "Terrain state cannot restore dynamic fragments; reset them explicitly first";
        return false;
    }
    auto* system = TerrainSystem::instance();
    const auto* source = reinterpret_cast<const std::uint8_t*>(bytes.data());
    flatbuffers::Verifier verifier(source, bytes.size());
    if (!runtime()->resource_ || !system || bytes.size() > 32U * 1024U * 1024U ||
        !schema::VerifyTerrainStateDataBuffer(verifier))
    {
        runtime()->last_error_ = "Invalid terrain state";
        return false;
    }
    const auto state = schema::UnPackTerrainStateData(source);
    TerrainVolume candidate;
    if (state->schema_version != 1 || state->base_guid != runtime()->resource_->info().guid ||
        state->base_hash != runtime()->resource_->info().content_hash || !state->volume ||
        !same_dimensions(*state->volume, runtime()->volume_.data()))
    {
        runtime()->last_error_ = "Terrain state does not match its base resource";
        return false;
    }
    return candidate.initialize(*state->volume, runtime()->last_error_) &&
           system->commit(*this, std::move(candidate));
}

bool TerrainComponent::save_state(const std::filesystem::path& path) const
{
    if (!valid())
    {
        return false;
    }
    ByteBuffer bytes;
    if (path.empty() || !snapshot(bytes))
    {
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec)
    {
        return false;
    }
    auto temporary = path;
    temporary += ".pending";
    const auto status = std::filesystem::symlink_status(temporary, ec);
    if ((ec && ec != std::errc::no_such_file_or_directory) ||
        status.type() != std::filesystem::file_type::not_found)
    {
        return false;
    }
    const auto temporary_text = PathUtils::to_utf8(temporary);
    const auto target_text = PathUtils::to_utf8(path);
    // Exclusive creation never follows an existing temporary file/symlink.
    auto* file = SDL_IOFromFile(temporary_text.c_str(), "wbx");
    if (!file)
    {
        return false;
    }
    const bool written = SDL_WriteIO(file, bytes.data(), bytes.size()) == bytes.size();
    const bool closed = SDL_CloseIO(file);
    if (!written || !closed)
    {
        (void)SDL_RemovePath(temporary_text.c_str());
        return false;
    }
    // SDL supplies replace-existing rename semantics on every supported platform.
    if (!SDL_RenamePath(temporary_text.c_str(), target_text.c_str()))
    {
        (void)SDL_RemovePath(temporary_text.c_str());
        return false;
    }
    return true;
}

bool TerrainComponent::load_state(const std::filesystem::path& path)
{
    if (!writable())
    {
        return false;
    }
    ByteBuffer bytes;
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size > 32U * 1024U * 1024U || !VirtualFileSystem::read_file(path, bytes))
    {
        runtime()->last_error_ = "Cannot read terrain state";
        return false;
    }
    return restore(bytes);
}

bool TerrainComponent::ready() const noexcept
{
    if (!valid())
    {
        return false;
    }
    return runtime()->ready_;
}
std::uint64_t TerrainComponent::revision() const noexcept
{
    if (!valid())
    {
        return {};
    }
    return runtime()->revision_;
}
std::size_t TerrainComponent::chunk_count() const noexcept
{
    if (!valid())
    {
        return {};
    }
    return runtime()->volume_.chunk_count();
}
std::size_t TerrainComponent::rebuilt_chunks() const noexcept
{
    if (!valid())
    {
        return {};
    }
    return runtime()->volume_.rebuilt_chunks();
}
std::size_t TerrainComponent::triangle_count() const noexcept
{
    if (!valid())
    {
        return {};
    }
    return runtime()->triangles_;
}
std::string TerrainComponent::last_error() const noexcept
{
    if (!valid())
    {
        return {};
    }
    return runtime()->last_error_;
}

} // namespace engine::plugins::terrain
