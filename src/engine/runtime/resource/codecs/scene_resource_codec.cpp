#include "runtime/resource/codecs/scene_resource_codec.hpp"

#include "runtime/resource/resource_catalog.hpp"
#include "runtime/resource/resource_system.hpp"

#include <flatbuffers/verifier.h>

#include <utility>

namespace engine
{

bool SceneResourceCodec::decode(const ResourceCatalog& catalog, ByteView payload,
                                Resource& output) const
{
    if (output.info().path.starts_with("builtin/"))
    {
        return false;
    }
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
    flatbuffers::Verifier verifier(bytes, payload.size());
    if (!schema::VerifySceneDataBuffer(verifier))
    {
        return false;
    }
    auto data = schema::UnPackSceneData(bytes);
    if (data->schema_version != 1 || !data->info)
    {
        return false;
    }
    for (const auto& node : data->nodes)
    {
        if (!node || !node->info)
        {
            return false;
        }
        for (const auto& component : node->components)
        {
            if (!component || !component->payload.value)
            {
                return false;
            }
            if (const auto* renderer = component->payload.AsMeshRendererData())
            {
                if (!declares(catalog, output, renderer->mesh_guid, schema::AssetKind::mesh) ||
                    !declares(catalog, output, renderer->material_guid,
                              schema::AssetKind::material))
                {
                    return false;
                }
                for (const auto& guid : renderer->additional_material_guids)
                {
                    if (!declares(catalog, output, guid, schema::AssetKind::material))
                    {
                        return false;
                    }
                }
            }
        }
    }
    store(output, std::move(data));
    return true;
}

bool SceneResourceCodec::validate_dependencies(const ResourceCatalog&, const Resource& resource,
                                               std::span<const ResourcePtr> dependencies) const
{
    const auto* scene = resource.scene();
    if (!scene)
    {
        return false;
    }
    for (const auto& node : scene->nodes)
    {
        for (const auto& component : node->components)
        {
            if (const auto* renderer = component->payload.AsMeshRendererData())
            {
                const auto* mesh = dependency(dependencies, renderer->mesh_guid);
                const auto* material = dependency(dependencies, renderer->material_guid);
                if (!mesh || !mesh->mesh() || !material || !material->material())
                {
                    return false;
                }
                for (const auto& guid : renderer->additional_material_guids)
                {
                    const auto* additional = dependency(dependencies, guid);
                    if (!additional || !additional->material())
                    {
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

} // namespace engine
