#pragma once
#include "editor/asset_pipeline/animation_sampling.hpp"

#include "resource_codecs.hpp"
#include "runtime/resource/builtin_resources.hpp"
#include "runtime/resource/resource_loader.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>

namespace test_support
{

inline std::vector<std::string>
scene_dependencies(const std::vector<std::unique_ptr<engine::schema::NodeDataT>>& nodes)
{
    std::vector<std::string> result;
    const auto add = [&](const std::string& guid)
    {
        if (engine::GuidMap::valid_guid(guid))
        {
            result.push_back(guid);
        }
    };
    for (const auto& node : nodes)
    {
        if (!node)
        {
            continue;
        }
        for (const auto& component : node->components)
        {
            if (!component)
            {
                continue;
            }
            if (const auto* value = component->payload.AsMeshRendererData())
            {
                add(value->mesh_guid);
                add(value->material_guid);
                add(value->lightmap_guid);
                for (const auto& guid : value->additional_material_guids)
                {
                    add(guid);
                }
            }
            if (const auto* value = component->payload.AsColliderData())
            {
                add(value->mesh_guid);
            }
            if (const auto* value = component->payload.AsAnimatorData())
            {
                add(value->animation_guid);
            }
            if (const auto* value = component->payload.AsAudioSourceData())
            {
                add(value->clip_guid);
            }
            if (const auto* value = component->payload.AsUiData())
            {
                add(value->document_guid);
            }
            if (const auto* value = component->payload.AsLightProbeVolumeData())
            {
                add(value->probes_guid);
            }
            if (const auto* value = component->payload.AsEnvironmentData())
            {
                add(value->ibl_irradiance_guid);
                add(value->ibl_prefilter_guid);
                add(value->ibl_lut_guid);
            }
            if (const auto* value = component->payload.AsTerrainData())
            {
                add(value->terrain_guid);
                add(value->material_guid);
            }
            if (const auto* value = component->payload.AsDestructionData())
            {
                add(value->destruction_guid);
                add(value->material_guid);
            }
            if (const auto* value = component->payload.AsEffectData())
            {
                add(value->effect_guid);
            }
        }
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

template <typename Codec>
std::optional<typename Codec::ResourceType::Data> builtin(std::string_view path)
{
    for (const auto& info : engine::builtin_resource_catalog().entries)
    {
        if (info->path != path)
        {
            continue;
        }
        engine::ByteBuffer bytes;
        std::unique_ptr<engine::Resource> value;
        std::string error;
        Codec codec;
        if (!engine::builtin_resource_payload(info->guid, bytes) ||
            !codec.decode(*info, bytes, value, error) || !codec.validate_data(*value, error))
        {
            return {};
        }
        const auto* data = engine::resource_detail::data<typename Codec::ResourceType>(value.get());
        if (data)
        {
            return *data;
        }
    }
    return {};
}

// 测试通过正式实体与目录接口准备资源，避免绕过加载和所有权契约。
class ResourceFixture
{
public:
    ResourceFixture()
    {
        std::error_code error;
        root = std::filesystem::temp_directory_path(error);
        if (error)
        {
            root.clear();
            return;
        }
        root /= "engine-resource-fixture-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        std::filesystem::create_directories(root / "gen", error);
        if (error)
        {
            root.clear();
        }
        catalog.schema_version = 2;
        catalog.revision = "fixture";
#if defined(__ANDROID__)
        catalog.target_platform = "android";
#elif defined(_WIN32)
        catalog.target_platform = "windows";
#elif defined(__APPLE__)
        catalog.target_platform = "macos";
#else
        catalog.target_platform = "linux";
#endif
    }

    ~ResourceFixture()
    {
        std::error_code error;
        if (!root.empty())
        {
            std::filesystem::remove_all(root, error);
        }
    }

    ResourceFixture(const ResourceFixture&) = delete;
    ResourceFixture& operator=(const ResourceFixture&) = delete;

    bool add(const engine::schema::ShaderDataT& shader)
    {
        std::vector<std::string> dependencies;
        for (const auto& sampler : shader.samplers)
        {
            if (sampler && sampler->source != engine::schema::ShaderTextureSource::resource)
            {
                continue;
            }
            std::string guid;
            if (!sampler || !find_guid(sampler->default_texture, guid))
            {
                return false;
            }
            dependencies.push_back(std::move(guid));
        }
        flatbuffers::FlatBufferBuilder builder;
        engine::schema::FinishShaderDataBuffer(builder,
                                               engine::schema::ShaderData::Pack(builder, &shader));
        return add_entity(shader.path, engine::schema::AssetKind::shader, builder, dependencies);
    }

    bool add(const engine::schema::MeshDataT& mesh)
    {
        flatbuffers::FlatBufferBuilder builder;
        engine::schema::FinishMeshDataBuffer(builder,
                                             engine::schema::MeshData::Pack(builder, &mesh));
        return add_entity(mesh.path, engine::schema::AssetKind::mesh, builder, {});
    }

    bool add(std::string_view path, const engine::schema::PrefabDataT& prefab)
    {
        flatbuffers::FlatBufferBuilder builder;
        engine::schema::FinishPrefabDataBuffer(builder,
                                               engine::schema::PrefabData::Pack(builder, &prefab));
        return add_entity(path, engine::schema::AssetKind::prefab, builder,
                          scene_dependencies(prefab.nodes));
    }

    bool add(std::string_view path, const engine::schema::SceneDataT& scene)
    {
        flatbuffers::FlatBufferBuilder builder;
        engine::schema::FinishSceneDataBuffer(builder,
                                              engine::schema::SceneData::Pack(builder, &scene));
        return add_entity(path, engine::schema::AssetKind::scene, builder,
                          scene_dependencies(scene.nodes));
    }

    bool add(const engine::schema::TextureDataT& texture)
    {
        flatbuffers::FlatBufferBuilder builder;
        engine::schema::FinishTextureDataBuffer(
            builder, engine::schema::TextureData::Pack(builder, &texture));
        return add_entity(texture.path, engine::schema::AssetKind::texture, builder, {});
    }

    bool add(const engine::schema::LightProbeDataT& probes)
    {
        flatbuffers::FlatBufferBuilder builder;
        engine::schema::FinishLightProbeDataBuffer(
            builder, engine::schema::LightProbeData::Pack(builder, &probes));
        return add_entity(probes.path, engine::schema::AssetKind::light_probe, builder, {});
    }

    bool add(const engine::schema::SkeletonDataT& skeleton)
    {
        auto compiled = skeleton;
        std::string error;
        if (!engine::editor::asset_pipeline::prepare_skeleton_sampling(compiled, error))
        {
            return false;
        }
        flatbuffers::FlatBufferBuilder builder;
        engine::schema::FinishSkeletonDataBuffer(
            builder, engine::schema::SkeletonData::Pack(builder, &compiled));
        return add_entity(skeleton.path, engine::schema::AssetKind::skeleton, builder, {});
    }

    bool add(const engine::schema::AnimationRetargetProfileDataT& profile)
    {
        flatbuffers::FlatBufferBuilder builder;
        engine::schema::FinishAnimationRetargetProfileDataBuffer(
            builder, engine::schema::AnimationRetargetProfileData::Pack(builder, &profile));
        std::vector<std::string> dependencies{profile.source_skeleton_guid,
                                              profile.target_skeleton_guid};
        return add_entity(profile.path, engine::schema::AssetKind::animation_retarget_profile,
                          builder, dependencies);
    }

    bool add(const engine::schema::MaterialDataT& definition)
    {
        engine::schema::MaterialDataT material(definition);
        if (!find_guid(material.shader_path, material.shader_guid))
        {
            return false;
        }
        std::vector<std::string> dependencies{material.shader_guid};
        for (auto& texture : material.textures)
        {
            if (!texture || !find_guid(texture->texture_path, texture->texture_guid))
            {
                return false;
            }
            dependencies.push_back(texture->texture_guid);
        }
        flatbuffers::FlatBufferBuilder builder;
        engine::schema::FinishMaterialDataBuffer(
            builder, engine::schema::MaterialData::Pack(builder, &material));
        return add_entity(material.type_path, engine::schema::AssetKind::material, builder,
                          dependencies);
    }

    bool publish(engine::ResourceSystem& resources)
    {
        catalog.revision = "fixture-" + std::to_string(catalog.entries.size());
        return resources.update_catalog(catalog, {{"content", root}});
    }

    bool load(engine::ResourceLoader& loader, std::string_view path, engine::ResourceHandle& output)
    {
        return loader.load(path, output);
    }

    bool read(engine::ResourceSystem& resources, std::string_view path,
              std::unique_ptr<engine::Resource>& output)
    {
        engine::Guid guid;
        return resources.catalog().identities().path_to_guid(path, guid) &&
               resources.read(guid, output);
    }

    std::filesystem::path root;
    engine::schema::AssetCatalogDataT catalog;

private:
    bool find_guid(std::string_view path, std::string& guid) const
    {
        for (const auto& entry : catalog.entries)
        {
            if (entry->path == path)
            {
                guid = entry->guid;
                return true;
            }
        }
        for (const auto& entry : engine::builtin_resource_catalog().entries)
        {
            if (entry->path == path)
            {
                guid = entry->guid;
                return true;
            }
        }
        return false;
    }

    bool add_entity(std::string_view path, engine::schema::AssetKind kind,
                    const flatbuffers::FlatBufferBuilder& builder,
                    const std::vector<std::string>& dependencies)
    {
        if (root.empty())
        {
            return false;
        }
        auto entry = std::make_unique<engine::schema::AssetEntryT>();
        char guid[37]{};
        std::snprintf(guid, sizeof(guid), "10000000-0000-4000-8000-%012zu",
                      catalog.entries.size() + 1);
        entry->guid = guid;
        entry->path = path;
        entry->kind = kind;
        entry->dependencies = dependencies;
        std::sort(entry->dependencies.begin(), entry->dependencies.end());
        entry->dependencies.erase(
            std::unique(entry->dependencies.begin(), entry->dependencies.end()),
            entry->dependencies.end());
        engine::ByteBuffer entity;
        const engine::ByteView payload(
            reinterpret_cast<const std::byte*>(builder.GetBufferPointer()), builder.GetSize());
        if (!engine::ResourceSystem::encode_resource(entry->guid, kind, payload, entity,
                                                     entry->header_size))
        {
            return false;
        }
        std::uint64_t hash = 14695981039346656037ULL;
        for (const auto value : entity)
        {
            hash = (hash ^ std::to_integer<std::uint8_t>(value)) * 1099511628211ULL;
        }
        char digest[17]{};
        std::snprintf(digest, sizeof(digest), "%016llx", static_cast<unsigned long long>(hash));
        entry->content_hash = digest;
        entry->content_size = entity.size();
        entry->location = std::make_unique<engine::schema::FileLocationT>();
        entry->location->mount = "content";
        entry->location->path = "gen/" + entry->guid;
        entry->location->length = entity.size();
        std::ofstream file(root / entry->location->path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(entity.data()),
                   static_cast<std::streamsize>(entity.size()));
        file.close();
        if (!file)
        {
            return false;
        }
        catalog.entries.push_back(std::move(entry));
        return true;
    }
};

} // namespace test_support
