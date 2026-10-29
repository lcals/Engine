#pragma once

#include "runtime/resource/resource_system.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace test_support
{

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

    bool add(const engine::schema::TextureDataT& texture)
    {
        flatbuffers::FlatBufferBuilder builder;
        engine::schema::FinishTextureDataBuffer(
            builder, engine::schema::TextureData::Pack(builder, &texture));
        return add_entity(texture.path, engine::schema::AssetKind::texture, builder, {});
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

    bool load(std::string_view path, engine::ResourcePtr& output)
    {
        auto* resources = engine::ResourceSystem::instance();
        engine::Guid guid;
        return resources && engine::ResourceSystem::path_to_guid(path, guid) &&
               resources->load(guid, output);
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
        return engine::ResourceSystem::path_to_guid(path, guid);
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
