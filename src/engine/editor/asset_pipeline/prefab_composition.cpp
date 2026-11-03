#include "editor/asset_pipeline/prefab_composition.hpp"

#include "editor/asset_pipeline/components/scene_component_codec.hpp"
#include "editor/asset_pipeline/importer_utils.hpp"
#include "editor/asset_pipeline/prefab_importer.hpp"
#include "editor/asset_pipeline/scene_importer.hpp"

#include <algorithm>
#include <flatbuffers/verifier.h>
#include <limits>
#include <set>

namespace engine::editor::asset_pipeline
{
bool scan_node_dependencies(const std::vector<std::unique_ptr<schema::NodeDataT>>& nodes,
                            const ImportContext& context, std::vector<Guid>& dependencies,
                            std::string& error)
{
    for (const auto& node : nodes)
    {
        for (const auto& component : node->components)
        {
            const auto* codec = find_scene_component_codec(component->payload.type);
            if (codec == nullptr ||
                !codec->scan_dependencies(*component, context, dependencies, error))
            {
                if (error.empty())
                {
                    error = "scene component dependency codec is unavailable";
                }
                return false;
            }
            if (const auto* environment = component->payload.AsEnvironmentData(); environment)
            {
                for (const auto* guid :
                     {&environment->ibl_irradiance_guid, &environment->ibl_prefilter_guid,
                      &environment->ibl_lut_guid})
                {
                    if (!guid->empty() &&
                        !reference(*guid, schema::AssetKind::texture, context, dependencies, error))
                    {
                        return false;
                    }
                }
            }
            if (const auto* ui = component->payload.AsUiData(); ui && !ui->document_guid.empty())
            {
                if (!reference(ui->document_guid, schema::AssetKind::ui, context, dependencies,
                               error))
                {
                    return false;
                }
            }
            if (const auto* text = component->payload.AsTextData();
                text && !text->font_guid.empty())
            {
                Guid shader;
                const auto path =
                    text->shader_path.empty() ? "builtin://shaders/font" : text->shader_path;
                if (!context.identities.path_to_guid(path, shader) ||
                    !reference(shader, schema::AssetKind::shader, context, dependencies, error) ||
                    !reference(text->font_guid, schema::AssetKind::font, context, dependencies,
                               error))
                {
                    if (error.empty())
                    {
                        error = "text references an unknown font shader";
                    }
                    return false;
                }
            }
            if (const auto* animator = component->payload.AsAnimatorData();
                animator != nullptr && !animator->animation_guid.empty())
            {
                if (!reference(animator->animation_guid, schema::AssetKind::animation, context,
                               dependencies, error))
                {
                    return false;
                }
            }
            if (const auto* audio = component->payload.AsAudioSourceData();
                audio != nullptr && !audio->clip_guid.empty())
            {
                if (!reference(audio->clip_guid, schema::AssetKind::audio, context, dependencies,
                               error))
                {
                    return false;
                }
            }
            if (const auto* volume = component->payload.AsLightProbeVolumeData();
                volume && !volume->probes_guid.empty())
            {
                if (!reference(volume->probes_guid, schema::AssetKind::light_probe, context,
                               dependencies, error))
                {
                    return false;
                }
            }
            if (const auto* mesh = component->payload.AsMeshRendererData())
            {
                if (!reference(mesh->mesh_guid, schema::AssetKind::mesh, context, dependencies,
                               error) ||
                    !reference(mesh->material_guid, schema::AssetKind::material, context,
                               dependencies, error))
                {
                    return false;
                }
                if (!mesh->lightmap_guid.empty() &&
                    !reference(mesh->lightmap_guid, schema::AssetKind::texture, context,
                               dependencies, error))
                {
                    return false;
                }
                for (const auto& material : mesh->additional_material_guids)
                {
                    if (!reference(material, schema::AssetKind::material, context, dependencies,
                                   error))
                    {
                        return false;
                    }
                }
            }
            if (const auto* collider = component->payload.AsColliderData();
                collider != nullptr && collider->shape == schema::ColliderShape::mesh)
            {
                if (!reference(collider->mesh_guid, schema::AssetKind::mesh, context, dependencies,
                               error))
                {
                    return false;
                }
            }
        }
    }
    std::sort(dependencies.begin(), dependencies.end());
    dependencies.erase(std::unique(dependencies.begin(), dependencies.end()), dependencies.end());
    return true;
}

namespace
{
bool scan_value(const toml::node& value, bool required, const ImportContext& context,
                std::vector<Guid>& dependencies, std::string& error)
{
    if (const auto* array = value.as_array())
    {
        for (const auto& element : *array)
        {
            if (!scan_value(element, required, context, dependencies, error))
            {
                return false;
            }
        }
    }
    else if (const auto* table = value.as_table())
    {
        for (const auto& [key, element] : *table)
        {
            if (!scan_value(element, required, context, dependencies, error))
            {
                return false;
            }
        }
    }
    else if (const auto text = value.value<std::string>())
    {
        Guid guid;
        if (GuidMap::valid_guid(*text))
        {
            guid = *text;
        }
        else if (!context.identities.path_to_guid(*text, guid))
        {
            return true;
        }
        const auto kind = context.kinds.find(guid);
        if (kind == context.kinds.end())
        {
            if (!required)
            {
                return true;
            }
            error = "Override references an unknown resource: " + guid;
            return false;
        }
        return reference(guid, kind->second, context, dependencies, error);
    }
    return true;
}
} // namespace

bool scan_override_dependencies(
    const std::vector<std::unique_ptr<schema::PrefabNodeOverrideT>>& overrides,
    const ImportContext& context, std::vector<Guid>& dependencies, std::string& error)
{
    for (const auto& node : overrides)
    {
        if (!node || node->removed)
        {
            continue;
        }
        for (const auto& component : node->components)
        {
            if (!component || component->removed)
            {
                continue;
            }
            toml::table data;
            if (!component->component ||
                !SceneImporter::write_component(*component->component, data, error))
            {
                return false;
            }
            const auto* codec = find_scene_component_codec(component->component->payload.type);
            if (!codec)
            {
                return false;
            }
            // New components can use their full payload. Unselected obsolete references
            // are not required, while final runtime dependencies use the composed result.
            for (const auto key : codec->resource_fields())
            {
                const auto* value = data.get(key);
                if (!value)
                {
                    continue;
                }
                const bool selected = std::find(component->fields.begin(), component->fields.end(),
                                                key) != component->fields.end();
                if (!scan_value(*value, selected, context, dependencies, error))
                {
                    return false;
                }
            }
        }
    }
    return true;
}

bool compose_scene(schema::SceneDataT& scene, const ImportContext& context, std::string& error)
{
    schema::SceneDataT candidate(scene);
    std::uint64_t maximum = 0;
    std::set<std::uint64_t> reserved;
    for (const auto& node : candidate.nodes)
    {
        if (!node || !node->info || !reserved.insert(node->info->properties->id).second)
        {
            error = "Scene contains invalid or duplicate ordinary nodes";
            return false;
        }
        maximum = std::max(maximum, node->info->properties->id);
    }
    for (const auto& instance : candidate.prefab_instances)
    {
        if (!instance)
        {
            return false;
        }
        for (const auto& mapping : instance->node_mappings)
        {
            if (!mapping || !reserved.insert(mapping->target_id).second)
            {
                error = "Prefab instance mappings overlap existing scene identities";
                return false;
            }
            maximum = std::max(maximum, mapping->target_id);
        }
    }
    for (auto& instance : candidate.prefab_instances)
    {
        std::vector<std::byte> payload;
        if (!read_generated_payload(instance->prefab_guid, context, payload, error))
        {
            return false;
        }
        flatbuffers::Verifier verifier(reinterpret_cast<const std::uint8_t*>(payload.data()),
                                       payload.size());
        if (!schema::VerifyPrefabDataBuffer(verifier))
        {
            error = "Invalid generated prefab: " + instance->prefab_guid;
            return false;
        }
        const auto prefab = schema::UnPackPrefabData(payload.data());
        std::vector<std::unique_ptr<schema::NodeDataT>> expanded;
        if (!PrefabImporter::expand_instance(
                *prefab, *instance,
                maximum == std::numeric_limits<std::uint64_t>::max() ? 0 : maximum + 1, expanded,
                error))
        {
            return false;
        }
        for (const auto& mapping : instance->node_mappings)
        {
            maximum = std::max(maximum, mapping->target_id);
        }
        for (auto& node : expanded)
        {
            candidate.nodes.push_back(std::move(node));
        }
    }
    if (!SceneImporter::validate(candidate, error))
    {
        return false;
    }
    scene = std::move(candidate);
    return true;
}
} // namespace engine::editor::asset_pipeline
