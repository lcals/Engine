#include "runtime/resource/codecs/scene_resource_codec.hpp"
#include "runtime/resource/codecs/resource_decode.hpp"

#include "runtime/core/guid_map.hpp"
#include "runtime/resource/types/animation_resource.hpp"
#include "runtime/resource/types/audio_resource.hpp"
#include "runtime/resource/types/font_resource.hpp"
#include "runtime/resource/types/light_probe_resource.hpp"
#include "runtime/resource/types/material_resource.hpp"
#include "runtime/resource/types/mesh_resource.hpp"
#include "runtime/resource/types/shader_resource.hpp"
#include "runtime/resource/types/texture_resource.hpp"
#include "runtime/resource/types/ui_resource.hpp"
#include "runtime/scene/scene_settings_validation.hpp"
#include <set>

#include <algorithm>

#include <utility>

namespace engine
{

schema::AssetKind SceneResourceCodec::kind() const noexcept
{
    return schema::AssetKind::scene;
}

bool SceneResourceCodec::decode(const schema::AssetEntryT& entry, ByteView payload,
                                std::unique_ptr<Resource>& output, std::string& error) const
{
    const auto validate = [](ByteView payload, std::string& error)
    {
        return resource_decode_detail::verify_flatbuffer(payload, schema::VerifySceneDataBuffer,
                                                         "Invalid Scene FlatBuffer", error);
    };
    return resource_decode_detail::decode_flatbuffer<ResourceType>(
        entry, payload, 64U * 1024U * 1024U, validate,
        [](const std::uint8_t* bytes) { return schema::UnPackSceneData(bytes); }, output, error);
}

bool SceneResourceCodec::validate_data(const Resource& resource, std::string& error) const
{
    const auto* data = resource_detail::data<ResourceType>(&resource);
    if (!data || !(!resource.info().path.starts_with("builtin://") && data->schema_version == 1 &&
                   data->info && validate_nodes(resource, data->nodes)))
    {
        error = "Invalid Scene data or declared dependencies";
        return false;
    }
    error.clear();
    return true;
}

bool SceneResourceCodec::validate_nodes(
    const Resource& resource, const std::vector<std::unique_ptr<schema::NodeDataT>>& nodes)
{
    bool has_environment = false;
    bool has_shadow_light = false;
    for (const auto& node : nodes)
    {
        if (!node || !node->info || !node->info->properties)
        {
            return false;
        }
        bool has_camera = false;
        bool has_post_process = false;
        for (const auto& component : node->components)
        {
            if (!component || !component->flags || !component->payload.value)
            {
                return false;
            }
            if (const auto* environment = component->payload.AsEnvironmentData())
            {
                if (!scene_detail::valid_environment(*environment) ||
                    (component->flags->enabled && has_environment))
                {
                    return false;
                }
                has_environment = has_environment || component->flags->enabled;
                for (const auto* guid :
                     {&environment->ibl_irradiance_guid, &environment->ibl_prefilter_guid,
                      &environment->ibl_lut_guid})
                {
                    if (!guid->empty() && !declares(resource, *guid))
                    {
                        return false;
                    }
                }
            }
            if (const auto* post_process = component->payload.AsPostProcessData())
            {
                if (!scene_detail::valid_post_process(*post_process))
                {
                    return false;
                }
                has_post_process = true;
            }
            if (const auto* planar = component->payload.AsPlanarReflectionData();
                planar && !scene_detail::valid_planar_reflection(*planar))
            {
                return false;
            }
            if (const auto* volume = component->payload.AsLightProbeVolumeData();
                volume && !volume->probes_guid.empty() && !declares(resource, volume->probes_guid))
            {
                return false;
            }
            has_camera = has_camera || component->payload.AsCameraData() != nullptr;
            if (const auto* terrain = component->payload.AsTerrainData())
            {
                if ((!terrain->terrain_guid.empty() &&
                     !declares(resource, terrain->terrain_guid)) ||
                    (!terrain->material_guid.empty() &&
                     !declares(resource, terrain->material_guid)))
                {
                    return false;
                }
            }
            if (const auto* destruction = component->payload.AsDestructionData())
            {
                if ((!destruction->destruction_guid.empty() &&
                     !declares(resource, destruction->destruction_guid)) ||
                    (!destruction->material_guid.empty() &&
                     !declares(resource, destruction->material_guid)))
                {
                    return false;
                }
            }
            if (const auto* light = component->payload.AsLightData())
            {
                if (!scene_detail::valid_light(*light) ||
                    (component->flags->enabled && light->cast_shadows &&
                     light->kind == schema::LightKind::directional && has_shadow_light))
                {
                    return false;
                }
                has_shadow_light =
                    has_shadow_light || (component->flags->enabled && light->cast_shadows &&
                                         light->kind == schema::LightKind::directional);
            }
            if (const auto* effect = component->payload.AsEffectData();
                effect && !effect->effect_guid.empty())
            {
                if (!declares(resource, effect->effect_guid))
                {
                    return false;
                }
            }
            if (const auto* ui = component->payload.AsUiData(); ui && !ui->document_guid.empty())
            {
                if (!declares(resource, ui->document_guid))
                {
                    return false;
                }
            }
            if (const auto* text = component->payload.AsTextData();
                text && !text->font_guid.empty())
            {
                const auto path =
                    text->shader_path.empty() ? "builtin://shaders/font" : text->shader_path;
                if (!valid_path(path) || !declares(resource, text->font_guid))
                {
                    return false;
                }
            }
            if (const auto* animator = component->payload.AsAnimatorData();
                animator != nullptr && !animator->animation_guid.empty())
            {
                if (!declares(resource, animator->animation_guid))
                {
                    return false;
                }
            }
            if (const auto* audio = component->payload.AsAudioSourceData();
                audio != nullptr && !audio->clip_guid.empty())
            {
                if (!declares(resource, audio->clip_guid))
                {
                    return false;
                }
            }
            if (const auto* renderer = component->payload.AsMeshRendererData())
            {
                if (!declares(resource, renderer->mesh_guid) ||
                    !declares(resource, renderer->material_guid))
                {
                    return false;
                }
                if (renderer->lightmap_guid.empty()
                        ? (!renderer->lightmap_mesh_hash.empty() ||
                           !renderer->lightmap_transform.empty())
                        : (!declares(resource, renderer->lightmap_guid) ||
                           renderer->lightmap_mesh_hash.empty() ||
                           renderer->lightmap_transform.size() != 16 ||
                           !std::all_of(renderer->lightmap_transform.begin(),
                                        renderer->lightmap_transform.end(),
                                        [](float value) { return std::isfinite(value); })))
                {
                    return false;
                }
                for (const auto& guid : renderer->additional_material_guids)
                {
                    if (!declares(resource, guid))
                    {
                        return false;
                    }
                }
            }
            if (const auto* collider = component->payload.AsColliderData();
                collider != nullptr && collider->shape == schema::ColliderShape::mesh)
            {
                if (!declares(resource, collider->mesh_guid))
                {
                    return false;
                }
            }
        }
        if (has_post_process && !has_camera)
        {
            return false;
        }
    }
    return true;
}

bool SceneResourceCodec::validate_dependencies(const Resource& resource, ResourceView dependencies,
                                               std::string& error) const
{
    const auto* scene = resource_detail::data<SceneResource>(&resource);
    if (!scene)
    {
        return false;
    }
    return validate_node_dependencies(scene->nodes, dependencies);
}

bool SceneResourceCodec::validate_node_dependencies(
    const std::vector<std::unique_ptr<schema::NodeDataT>>& nodes, ResourceView dependencies)
{
    for (const auto& node : nodes)
    {
        for (const auto& component : node->components)
        {
            if (const auto* environment = component->payload.AsEnvironmentData();
                environment && !environment->ibl_irradiance_guid.empty())
            {
                const auto* irradiance = dependency(dependencies, environment->ibl_irradiance_guid);
                const auto* prefilter = dependency(dependencies, environment->ibl_prefilter_guid);
                const auto* lut = dependency(dependencies, environment->ibl_lut_guid);
                const auto* diffuse_data =
                    irradiance ? resource_detail::data<TextureResource>(irradiance) : nullptr;
                const auto* specular_data =
                    prefilter ? resource_detail::data<TextureResource>(prefilter) : nullptr;
                const auto* lut_data = lut ? resource_detail::data<TextureResource>(lut) : nullptr;
                if (diffuse_data == nullptr || specular_data == nullptr || lut_data == nullptr ||
                    !diffuse_data->cube_map || !specular_data->cube_map || lut_data->cube_map ||
                    diffuse_data->srgb || specular_data->srgb || lut_data->srgb ||
                    diffuse_data->format != schema::TextureFormat::rgba16f ||
                    specular_data->format != schema::TextureFormat::rgba16f ||
                    lut_data->format != schema::TextureFormat::rg16f)
                {
                    return false;
                }
            }
            if (const auto* terrain = component->payload.AsTerrainData())
            {
                const auto* value = dependency(dependencies, terrain->terrain_guid);
                const auto* material = dependency(dependencies, terrain->material_guid);
                if ((!terrain->terrain_guid.empty() &&
                     (!value || value->kind() != schema::AssetKind::terrain)) ||
                    (!terrain->material_guid.empty() &&
                     !resource_detail::data<MaterialResource>(material)))
                {
                    return false;
                }
            }
            if (const auto* destruction = component->payload.AsDestructionData())
            {
                const auto* value = dependency(dependencies, destruction->destruction_guid);
                const auto* material = dependency(dependencies, destruction->material_guid);
                if ((!destruction->destruction_guid.empty() &&
                     (!value || value->kind() != schema::AssetKind::destruction)) ||
                    (!destruction->material_guid.empty() &&
                     !resource_detail::data<MaterialResource>(material)))
                {
                    return false;
                }
            }
            if (const auto* effect = component->payload.AsEffectData();
                effect && !effect->effect_guid.empty())
            {
                const auto* effect_resource = dependency(dependencies, effect->effect_guid);
                if (!effect_resource || effect_resource->info().kind != schema::AssetKind::effect)
                {
                    return false;
                }
            }
            if (const auto* ui = component->payload.AsUiData(); ui && !ui->document_guid.empty())
            {
                const auto* document = dependency(dependencies, ui->document_guid);
                if (!document || !resource_detail::data<UiResource>(document))
                {
                    return false;
                }
            }
            if (const auto* text = component->payload.AsTextData();
                text && !text->font_guid.empty())
            {
                const auto* font = dependency(dependencies, text->font_guid);
                const auto path =
                    text->shader_path.empty() ? "builtin://shaders/font" : text->shader_path;
                if (!font || !resource_detail::data<FontResource>(font))
                {
                    return false;
                }
                const auto* shader_resource = dependency(dependencies, path);
                if (!shader_resource || !resource_detail::data<ShaderResource>(shader_resource))
                {
                    return false;
                }
            }
            if (const auto* animator = component->payload.AsAnimatorData();
                animator != nullptr && !animator->animation_guid.empty())
            {
                const auto* animation = dependency(dependencies, animator->animation_guid);
                if (!animation || !resource_detail::data<AnimationResource>(animation))
                {
                    return false;
                }
            }
            if (const auto* audio = component->payload.AsAudioSourceData();
                audio != nullptr && !audio->clip_guid.empty())
            {
                const auto* clip = dependency(dependencies, audio->clip_guid);
                if (clip == nullptr || resource_detail::data<AudioResource>(clip) == nullptr)
                {
                    return false;
                }
            }
            if (const auto* volume = component->payload.AsLightProbeVolumeData();
                volume && !volume->probes_guid.empty())
            {
                const auto* probes = dependency(dependencies, volume->probes_guid);
                if (!probes || !resource_detail::data<LightProbeResource>(probes))
                {
                    return false;
                }
            }
            if (const auto* renderer = component->payload.AsMeshRendererData())
            {
                const auto* mesh = dependency(dependencies, renderer->mesh_guid);
                const auto* material = dependency(dependencies, renderer->material_guid);
                if (!mesh || !resource_detail::data<MeshResource>(mesh) || !material ||
                    !resource_detail::data<MaterialResource>(material))
                {
                    return false;
                }
                if (!renderer->lightmap_guid.empty())
                {
                    const auto* lightmap = dependency(dependencies, renderer->lightmap_guid);
                    if (!lightmap || !resource_detail::data<TextureResource>(lightmap) ||
                        resource_detail::data<TextureResource>(lightmap)->srgb ||
                        resource_detail::data<TextureResource>(lightmap)->cube_map ||
                        resource_detail::data<MeshResource>(mesh)->lightmap_texcoords.size() !=
                            resource_detail::data<MeshResource>(mesh)->vertices.size())
                    {
                        return false;
                    }
                }
                for (const auto& guid : renderer->additional_material_guids)
                {
                    const auto* additional = dependency(dependencies, guid);
                    if (!additional || !resource_detail::data<MaterialResource>(additional))
                    {
                        return false;
                    }
                }
            }
            if (const auto* collider = component->payload.AsColliderData();
                collider != nullptr && collider->shape == schema::ColliderShape::mesh)
            {
                const auto* mesh = dependency(dependencies, collider->mesh_guid);
                if (mesh == nullptr || resource_detail::data<MeshResource>(mesh) == nullptr)
                {
                    return false;
                }
            }
        }
    }
    return true;
}

} // namespace engine
