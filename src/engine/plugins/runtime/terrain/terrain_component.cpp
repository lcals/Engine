#include "plugins/runtime/terrain/terrain_component.hpp"

#include "plugins/runtime/terrain/terrain_system.hpp"
#include "runtime/performance/profiling.hpp"
#include "runtime/core/guid_map.hpp"
#include "runtime/resource/resource_system.hpp"
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
bool read_volume(ByteView bytes, schema::TerrainAssetDataT& data, std::string& error)
{
    const auto* source = reinterpret_cast<const std::uint8_t*>(bytes.data());
    flatbuffers::Verifier verifier(source, bytes.size());
    if (!schema::VerifyTerrainAssetDataBuffer(verifier))
    {
        error = "Invalid terrain resource buffer";
        return false;
    }
    data = *schema::UnPackTerrainAssetData(source);
    return TerrainVolume::validate(data, error);
}

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

TerrainComponent::TerrainComponent(Node& node) : Component(node)
{
    mutable_data().payload.Set(schema::TerrainDataT{});
}

bool TerrainComponent::valid_settings(const schema::TerrainDataT& settings) noexcept
{
    return (settings.terrain_guid.empty() || GuidMap::valid_guid(settings.terrain_guid)) &&
           (settings.material_guid.empty() || GuidMap::valid_guid(settings.material_guid)) &&
           color_valid(settings.surface_color) && color_valid(settings.wall_color) &&
           std::isfinite(settings.uv_scale) && settings.uv_scale > 0 && settings.uv_scale <= 100;
}

const schema::TerrainDataT& TerrainComponent::settings() const noexcept
{
    return *data().payload.AsTerrainData();
}

bool TerrainComponent::set_settings(const schema::TerrainDataT& value)
{
    if (!valid_settings(value))
    {
        last_error_ = "Invalid terrain component settings";
        return false;
    }
    auto* resources = ResourceSystem::instance();
    ResourcePtr replacement;
    TerrainVolume candidate;
    if (!value.terrain_guid.empty())
    {
        schema::TerrainAssetDataT asset;
        if (!resources || !resources->loader().load(value.terrain_guid, replacement) ||
            !replacement || replacement->info().kind != schema::AssetKind::terrain ||
            !read_volume(replacement->bytes(), asset, last_error_) ||
            !candidate.initialize(asset, last_error_))
        {
            last_error_ = "Cannot load terrain resource";
            return false;
        }
        if (!value.material_guid.empty())
        {
            ResourcePtr material;
            if (!resources->loader().load(value.material_guid, material) || !material ||
                !material->material())
            {
                last_error_ = "Cannot load terrain material";
                return false;
            }
        }
    }
    const bool retain = resource_ && replacement &&
                        resource_->info().guid == replacement->info().guid &&
                        resource_->info().content_hash == replacement->info().content_hash;
    if (retain)
    {
        if (detached_ && value != settings())
        {
            last_error_ = "Reset terrain fragments before changing their material or settings";
            return false;
        }
        candidate = volume_;
    }
    const auto previous_settings = settings();
    auto previous_resource = resource_;
    *mutable_data().payload.AsTerrainData() = value;
    resource_ = std::move(replacement);
    if (ready_ && resource_)
    {
        auto* system = TerrainSystem::instance();
        if (!system || !system->commit(*this, std::move(candidate)))
        {
            *mutable_data().payload.AsTerrainData() = previous_settings;
            resource_ = std::move(previous_resource);
            return false;
        }
    }
    else
    {
        volume_ = std::move(candidate);
        triangles_ = 0;
        ready_ = false;
        pending_ = resource_ != nullptr;
    }
    if (!retain)
    {
        if (auto* system = TerrainSystem::instance())
        {
            system->clear_fragments(*this);
        }
        detached_ = false;
    }
    last_error_.clear();
    return true;
}

bool TerrainComponent::restore_data(const schema::ComponentDataT& data)
{
    const auto* value = data.payload.AsTerrainData();
    if (!value || !set_settings(*value))
    {
        return false;
    }
    set_enabled(data.enabled);
    return true;
}

bool TerrainComponent::apply_sphere(const Vec3& center, float radius, bool fill)
{
    ENGINE_PROFILE_ZONE("Terrain.brush");
    auto* system = TerrainSystem::instance();
    if (!ready_ || !system || !enabled())
    {
        last_error_ = "Terrain is not ready";
        return false;
    }
    auto candidate = volume_;
    return candidate.apply_sphere(center, radius, fill, last_error_) &&
           system->commit(*this, std::move(candidate));
}

bool TerrainComponent::retire_fragments(std::uint32_t max_count, double max_age_seconds,
                                       std::uint32_t& retired_count)
{
    retired_count = 0;
    auto* system = TerrainSystem::instance();
    if (!system)
    {
        last_error_ = "Terrain system is not ready";
        return false;
    }
    return system->retire_fragments(*this, max_count, max_age_seconds, retired_count);
}

bool TerrainComponent::reset()
{
    auto* system = TerrainSystem::instance();
    schema::TerrainAssetDataT data;
    TerrainVolume candidate;
    if (!resource_ || !system || !read_volume(resource_->bytes(), data, last_error_) ||
        !candidate.initialize(data, last_error_))
    {
        return false;
    }
    if (!system->commit(*this, std::move(candidate)))
    {
        return false;
    }
    system->clear_fragments(*this);
    detached_ = false;
    return true;
}

bool TerrainComponent::collapse_detached(std::uint32_t min_samples, std::uint32_t max_fragments,
                                         std::uint32_t fragment_cells, float density)
{
    auto* system = TerrainSystem::instance();
    if (!system)
    {
        last_error_ = "Terrain system is not available";
        return false;
    }
    return system->collapse_detached(*this, min_samples, max_fragments, fragment_cells, density);
}

std::size_t TerrainComponent::fragment_count() const noexcept
{
    const auto* system = TerrainSystem::instance();
    return system ? system->fragment_count(*this) : 0;
}

bool TerrainComponent::snapshot(ByteBuffer& output) const
{
    output.clear();
    if (!ready_ || !resource_ || detached_)
    {
        return false;
    }
    schema::TerrainStateDataT state;
    state.base_guid = resource_->info().guid;
    state.base_hash = resource_->info().content_hash;
    state.volume = std::make_unique<schema::TerrainAssetDataT>(volume_.data());
    flatbuffers::FlatBufferBuilder builder;
    schema::FinishTerrainStateDataBuffer(builder, schema::TerrainStateData::Pack(builder, &state));
    const auto* begin = reinterpret_cast<const std::byte*>(builder.GetBufferPointer());
    output.assign(begin, begin + builder.GetSize());
    return true;
}

bool TerrainComponent::restore(ByteView bytes)
{
    if (detached_)
    {
        last_error_ = "Terrain state cannot restore dynamic fragments; reset them explicitly first";
        return false;
    }
    auto* system = TerrainSystem::instance();
    const auto* source = reinterpret_cast<const std::uint8_t*>(bytes.data());
    flatbuffers::Verifier verifier(source, bytes.size());
    if (!resource_ || !system || bytes.size() > 32U * 1024U * 1024U ||
        !schema::VerifyTerrainStateDataBuffer(verifier))
    {
        last_error_ = "Invalid terrain state";
        return false;
    }
    const auto state = schema::UnPackTerrainStateData(source);
    TerrainVolume candidate;
    if (state->schema_version != 1 || state->base_guid != resource_->info().guid ||
        state->base_hash != resource_->info().content_hash || !state->volume ||
        !same_dimensions(*state->volume, volume_.data()))
    {
        last_error_ = "Terrain state does not match its base resource";
        return false;
    }
    return candidate.initialize(*state->volume, last_error_) &&
           system->commit(*this, std::move(candidate));
}

bool TerrainComponent::save_state(const std::filesystem::path& path) const
{
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
    ByteBuffer bytes;
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size > 32U * 1024U * 1024U || !ResourceSystem::read_binary(path, bytes))
    {
        last_error_ = "Cannot read terrain state";
        return false;
    }
    return restore(bytes);
}

bool TerrainComponent::ready() const noexcept
{
    return ready_;
}
std::uint64_t TerrainComponent::revision() const noexcept
{
    return revision_;
}
std::size_t TerrainComponent::chunk_count() const noexcept
{
    return volume_.chunk_count();
}
std::size_t TerrainComponent::rebuilt_chunks() const noexcept
{
    return volume_.rebuilt_chunks();
}
std::size_t TerrainComponent::triangle_count() const noexcept
{
    return triangles_;
}
const std::string& TerrainComponent::last_error() const noexcept
{
    return last_error_;
}

} // namespace engine::plugins::terrain
