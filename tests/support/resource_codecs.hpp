#pragma once

#include "runtime/resource/builtin_resources.hpp"
#include "runtime/resource/codecs/animation_resource_codec.hpp"
#include "runtime/resource/codecs/animation_retarget_resource_codec.hpp"
#include "runtime/resource/codecs/audio_resource_codec.hpp"
#include "runtime/resource/codecs/font_resource_codec.hpp"
#include "runtime/resource/codecs/json_resource_codec.hpp"
#include "runtime/resource/codecs/light_probe_resource_codec.hpp"
#include "runtime/resource/codecs/material_resource_codec.hpp"
#include "runtime/resource/codecs/mesh_resource_codec.hpp"
#include "runtime/resource/codecs/model_resource_codec.hpp"
#include "runtime/resource/codecs/movie_resource_codec.hpp"
#include "runtime/resource/codecs/prefab_resource_codec.hpp"
#include "runtime/resource/codecs/scene_resource_codec.hpp"
#include "runtime/resource/codecs/script_resource_codec.hpp"
#include "runtime/resource/codecs/shader_resource_codec.hpp"
#include "runtime/resource/codecs/skeleton_resource_codec.hpp"
#include "runtime/resource/codecs/source_resource_codec.hpp"
#include "runtime/resource/codecs/texture_resource_codec.hpp"
#include "runtime/resource/codecs/ui_resource_codec.hpp"
#include "runtime/resource/resource_system.hpp"

namespace test_support
{
inline bool read_resource(engine::ResourceSystem& resources, std::string_view path,
                          std::unique_ptr<engine::Resource>& output)
{
    engine::Guid guid;
    const auto* entry = resources.catalog().find(path);
    if (!entry && resources.catalog().identities().path_to_guid(path, guid))
    {
        entry = resources.catalog().find(guid);
    }
    if (!entry || output)
    {
        return false;
    }
    if (!entry->location && entry->path.starts_with("builtin://"))
    {
        // Fixture preparation explicitly supplies native builtins; physical entries still
        // go through the core reader and never fall back when their file is missing.
        engine::ByteBuffer bytes;
        std::string error;
        if (!engine::builtin_resource_payload(entry->guid, bytes))
        {
            return false;
        }
        const auto decode = [&](const auto& codec)
        {
            return codec.decode(*entry, bytes, output, error) && resources.validate_memory(*output);
        };
        switch (entry->kind)
        {
        case engine::schema::AssetKind::shader:
            return decode(engine::ShaderResourceCodec{});
        case engine::schema::AssetKind::texture:
            return decode(engine::TextureResourceCodec{});
        case engine::schema::AssetKind::material:
            return decode(engine::MaterialResourceCodec{});
        case engine::schema::AssetKind::mesh:
            return decode(engine::MeshResourceCodec{});
        default:
            return false;
        }
    }
    return resources.read(entry->guid, output);
}

inline bool validate_dependencies(engine::ResourceSystem& resources,
                                  const engine::Resource& resource,
                                  std::initializer_list<const engine::Resource*> dependencies)
{
    engine::ResourceError error = engine::ResourceError::none;
    std::string message;
    return resources.validate_dependencies(
        resource, engine::ResourceView(dependencies.begin(), dependencies.size()), error, message);
}

inline bool validate_dependencies(engine::ResourceSystem& resources,
                                  const engine::Resource& resource,
                                  const std::vector<const engine::Resource*>& dependencies)
{
    engine::ResourceError error = engine::ResourceError::none;
    std::string message;
    return resources.validate_dependencies(resource, dependencies, error, message);
}

inline bool register_resource_codecs(engine::ResourceSystem& resources)
{
    return resources.add<engine::SourceResourceCodec>() &&
           resources.add<engine::ScriptResourceCodec>() &&
           resources.add<engine::JsonResourceCodec>() &&
           resources.add<engine::SceneResourceCodec>() &&
           resources.add<engine::PrefabResourceCodec>() &&
           resources.add<engine::MeshResourceCodec>() &&
           resources.add<engine::ShaderResourceCodec>() &&
           resources.add<engine::MaterialResourceCodec>() &&
           resources.add<engine::TextureResourceCodec>() &&
           resources.add<engine::AudioResourceCodec>() &&
           resources.add<engine::MovieResourceCodec>() &&
           resources.add<engine::FontResourceCodec>() && resources.add<engine::UiResourceCodec>() &&
           resources.add<engine::LightProbeResourceCodec>() &&
           resources.add<engine::ModelResourceCodec>() &&
           resources.add<engine::SkeletonResourceCodec>() &&
           resources.add<engine::AnimationResourceCodec>() &&
           resources.add<engine::AnimationRetargetResourceCodec>();
}
} // namespace test_support
