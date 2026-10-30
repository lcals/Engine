#include "runtime/resource/codecs/scene_resource_codec.hpp"

#include "runtime/resource/resource_catalog.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/scene_settings_validation.hpp"

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
    bool has_environment = false;
    bool has_shadow_light = false;
    for (const auto& node : data->nodes)
    {
        if (!node || !node->info)
        {
            return false;
        }
        bool has_camera = false;
        bool has_post_process = false;
        for (const auto& component : node->components)
        {
            if (!component || !component->payload.value)
            {
                return false;
            }
            if (const auto* environment = component->payload.AsEnvironmentData())
            {
                if (!scene_detail::valid_environment(*environment) ||
                    (component->enabled && has_environment))
                {
                    return false;
                }
                has_environment = has_environment || component->enabled;
                for (const auto* guid :
                     {&environment->ibl_irradiance_guid, &environment->ibl_prefilter_guid,
                      &environment->ibl_lut_guid})
                {
                    if (!guid->empty() &&
                        !declares(catalog, output, *guid, schema::AssetKind::texture))
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
            has_camera = has_camera || component->payload.AsCameraData() != nullptr;
            if (const auto* light = component->payload.AsLightData())
            {
                if (!scene_detail::valid_light(*light) ||
                    (component->enabled && light->cast_shadows && has_shadow_light))
                {
                    return false;
                }
                has_shadow_light = has_shadow_light || (component->enabled && light->cast_shadows);
            }
            if (const auto* ui = component->payload.AsUiData(); ui && !ui->document_guid.empty())
            {
                if (!declares(catalog, output, ui->document_guid, schema::AssetKind::ui))
                {
                    return false;
                }
            }
            if (const auto* text = component->payload.AsTextData();
                text && !text->font_guid.empty())
            {
                Guid shader;
                const auto path =
                    text->shader_path.empty() ? "builtin/shaders/font" : text->shader_path;
                if (!catalog.identities().path_to_guid(path, shader) ||
                    !declares(catalog, output, shader, schema::AssetKind::shader) ||
                    !declares(catalog, output, text->font_guid, schema::AssetKind::font))
                {
                    return false;
                }
            }
            if (const auto* animator = component->payload.AsAnimatorData();
                animator != nullptr && !animator->animation_guid.empty())
            {
                if (!declares(catalog, output, animator->animation_guid,
                              schema::AssetKind::animation))
                {
                    return false;
                }
            }
            if (const auto* audio = component->payload.AsAudioSourceData();
                audio != nullptr && !audio->clip_guid.empty())
            {
                if (!declares(catalog, output, audio->clip_guid, schema::AssetKind::audio))
                {
                    return false;
                }
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
            if (const auto* collider = component->payload.AsColliderData();
                collider != nullptr && collider->shape == schema::ColliderShape::mesh)
            {
                if (!declares(catalog, output, collider->mesh_guid, schema::AssetKind::mesh))
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
    store(output, std::move(data));
    return true;
}

bool SceneResourceCodec::validate_dependencies(const ResourceCatalog& catalog,
                                               const Resource& resource,
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
            if (const auto* environment = component->payload.AsEnvironmentData();
                environment && !environment->ibl_irradiance_guid.empty())
            {
                const auto* irradiance = dependency(dependencies, environment->ibl_irradiance_guid);
                const auto* prefilter = dependency(dependencies, environment->ibl_prefilter_guid);
                const auto* lut = dependency(dependencies, environment->ibl_lut_guid);
                const auto* diffuse_data = irradiance ? irradiance->texture() : nullptr;
                const auto* specular_data = prefilter ? prefilter->texture() : nullptr;
                const auto* lut_data = lut ? lut->texture() : nullptr;
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
            if (const auto* ui = component->payload.AsUiData(); ui && !ui->document_guid.empty())
            {
                const auto* document = dependency(dependencies, ui->document_guid);
                if (!document || !document->ui())
                {
                    return false;
                }
            }
            if (const auto* text = component->payload.AsTextData();
                text && !text->font_guid.empty())
            {
                const auto* font = dependency(dependencies, text->font_guid);
                Guid shader;
                const auto path =
                    text->shader_path.empty() ? "builtin/shaders/font" : text->shader_path;
                if (!font || !font->font() || !catalog.identities().path_to_guid(path, shader))
                {
                    return false;
                }
                const auto* shader_resource = dependency(dependencies, shader);
                if (!shader_resource || !shader_resource->shader())
                {
                    return false;
                }
            }
            if (const auto* animator = component->payload.AsAnimatorData();
                animator != nullptr && !animator->animation_guid.empty())
            {
                const auto* animation = dependency(dependencies, animator->animation_guid);
                if (!animation || !animation->animation())
                {
                    return false;
                }
            }
            if (const auto* audio = component->payload.AsAudioSourceData();
                audio != nullptr && !audio->clip_guid.empty())
            {
                const auto* clip = dependency(dependencies, audio->clip_guid);
                if (clip == nullptr || clip->audio() == nullptr)
                {
                    return false;
                }
            }
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
            if (const auto* collider = component->payload.AsColliderData();
                collider != nullptr && collider->shape == schema::ColliderShape::mesh)
            {
                const auto* mesh = dependency(dependencies, collider->mesh_guid);
                if (mesh == nullptr || mesh->mesh() == nullptr)
                {
                    return false;
                }
            }
        }
    }
    return true;
}

} // namespace engine
