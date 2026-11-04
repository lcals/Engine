#include "editor/asset_pipeline/prefab_importer.hpp"
#include "editor/asset_pipeline/scene_importer.hpp"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace
{

using namespace engine;

bool expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "prefab fields test: %s\n", message);
    }
    return condition;
}

template <class T> schema::ComponentDataT component(const T& settings)
{
    schema::ComponentDataT value;
    value.flags = std::make_unique<schema::ComponentFlagsT>();
    value.payload.Set(T(settings));
    return value;
}

bool apply(schema::ComponentDataT& target, const schema::ComponentDataT& value,
           std::vector<std::string> fields)
{
    schema::PrefabComponentOverrideT override_data;
    override_data.component = std::make_unique<schema::ComponentDataT>(value);
    override_data.fields = std::move(fields);
    std::string error;
    return editor::PrefabImporter::apply_component_override(target, override_data, error);
}

bool mesh_optional_fields()
{
    schema::MeshRendererDataT settings;
    settings.mesh_guid = "50000000-0000-4000-8000-000000000001";
    settings.material_guid = "50000000-0000-4000-8000-000000000002";
    auto base = component(settings);
    auto changed = base;
    changed.payload.AsMeshRendererData()->additional_material_guids.push_back(
        "50000000-0000-4000-8000-000000000003");
    if (!expect(apply(base, changed, {"additional_materials"}) && base == changed,
                "adding the first optional material slot is a valid explicit override") ||
        !expect(apply(base, component(settings), {"additional_materials"}) &&
                    base.payload.AsMeshRendererData()->additional_material_guids.empty(),
                "an explicit empty material list clears all additional slots"))
    {
        return false;
    }
    auto baked = settings;
    baked.lightmap_guid = "50000000-0000-4000-8000-000000000004";
    baked.lightmap_mesh_hash = "fingerprint";
    baked.lightmap_transform.resize(16, 0);
    for (std::size_t index = 0; index < 16; index += 5)
    {
        baked.lightmap_transform[index] = 1;
    }
    if (!expect(apply(base, component(baked),
                      {"lightmap", "lightmap_mesh_hash", "lightmap_transform"}) &&
                    base == component(baked),
                "adding a previously absent lightmap supplies all explicit binding fields"))
    {
        return false;
    }
    const auto before = base;
    if (!expect(!apply(base, component(settings), {"lightmap"}) && base == before,
                "an incomplete lightmap clear fails atomically") ||
        !expect(apply(base, component(settings),
                      {"lightmap", "lightmap_mesh_hash", "lightmap_transform"}) &&
                    base == component(settings),
                "empty GUID, hash and matrix clear the optional lightmap together"))
    {
        return false;
    }
    auto hidden = base;
    hidden.flags->enabled = false;
    hidden.flags->visible = false;
    hidden.payload.AsMeshRendererData()->cast_shadows = false;
    return expect(apply(base, hidden, {"enabled", "visible", "cast_shadows"}) && base == hidden,
                  "false-valued overrides remain explicit rather than restoring defaults");
}

bool collider_optional_fields()
{
    const schema::ColliderDataT box;
    auto mesh = box;
    mesh.shape = schema::ColliderShape::mesh;
    mesh.mesh_guid = "50000000-0000-4000-8000-000000000001";
    auto base = component(box);
    if (!expect(apply(base, component(mesh), {"shape", "mesh"}) && base == component(mesh),
                "a box can acquire an explicit mesh binding") ||
        !expect(apply(base, component(box), {"shape", "mesh"}) && base == component(box),
                "switching back to box clears the optional mesh GUID"))
    {
        return false;
    }
    auto compound = box;
    compound.shape = schema::ColliderShape::compound;
    auto hull = std::make_unique<schema::ColliderHullDataT>();
    hull->vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    compound.hulls.push_back(std::move(hull));
    if (!expect(apply(base, component(compound), {"shape", "hulls"}) && base == component(compound),
                "a compound override can introduce hulls absent from the original collider"))
    {
        return false;
    }
    const auto before = base;
    return expect(!apply(base, component(box), {"hulls"}) && base == before,
                  "clearing hulls while leaving compound shape fails without changing the base") &&
           expect(apply(base, component(box), {"shape", "hulls"}) && base == component(box),
                  "explicit empty hulls are valid when restoring a primitive shape");
}

bool render_policy_fields()
{
    schema::MeshRendererDataT settings;
    settings.mesh_guid = "50000000-0000-4000-8000-000000000001";
    settings.material_guid = "50000000-0000-4000-8000-000000000002";
    if (!expect(!settings.static_instancing_enabled && !settings.static_lod_enabled &&
                    !schema::CameraDataT{}.frustum_culling_enabled,
                "generated optional runtime attributes default off"))
    {
        return false;
    }
    std::string error;
    toml::table missing_mesh{{"type", "mesh_renderer"},
                             {"mesh", settings.mesh_guid},
                             {"material", settings.material_guid}};
    schema::ComponentDataT default_mesh;
    schema::ComponentDataT default_camera;
    if (!expect(editor::SceneImporter::read_component(missing_mesh, default_mesh, error) &&
                    !default_mesh.payload.AsMeshRendererData()->static_instancing_enabled &&
                    !default_mesh.payload.AsMeshRendererData()->static_lod_enabled &&
                    editor::SceneImporter::read_component(toml::table{{"type", "camera"}},
                                                          default_camera, error) &&
                    !default_camera.payload.AsCameraData()->frustum_culling_enabled,
                "sources without optional attributes preserve the explicit default-off rule"))
    {
        return false;
    }
    settings.static_instancing_enabled = true;
    settings.static_lod_enabled = true;
    auto original = component(settings);
    settings.static_instancing_enabled = false;
    settings.static_lod_enabled = false;
    const auto changed = component(settings);
    if (!expect(apply(original, changed, {"static_instancing_enabled", "static_lod_enabled"}) &&
                    original == changed,
                "prefab instances can explicitly override mesh submission choices without touching "
                "templates"))
    {
        return false;
    }
    schema::CameraDataT camera;
    camera.frustum_culling_enabled = true;
    auto camera_original = component(camera);
    camera.frustum_culling_enabled = false;
    const auto camera_changed = component(camera);
    if (!expect(apply(camera_original, camera_changed, {"frustum_culling_enabled"}) &&
                    camera_original == camera_changed,
                "camera frustum policy is a normal prefab component override"))
    {
        return false;
    }
    for (const auto* value : {&changed, &camera_changed})
    {
        toml::table table;
        schema::ComponentDataT decoded;
        if (!expect(editor::SceneImporter::write_component(*value, table, error) &&
                        editor::SceneImporter::read_component(table, decoded, error) &&
                        decoded == *value,
                    "ordinary scene TOML round trips explicit false component choices"))
        {
            return false;
        }
        const auto* camera_data = value->payload.AsCameraData();
        for (const auto* field : camera_data ? std::vector<const char*>{"frustum_culling_enabled"}
                                             : std::vector<const char*>{"static_instancing_enabled",
                                                                        "static_lod_enabled"})
        {
            for (const auto* invalid : {"0", "\"false\"", "[]"})
            {
                auto malformed = table;
                auto parsed = toml::parse(std::string("value=") + invalid);
                malformed.insert_or_assign(field, *parsed["value"].node());
                schema::ComponentDataT rejected;
                error.clear();
                if (!expect(
                        !editor::SceneImporter::read_component(malformed, rejected, error) &&
                            !error.empty(),
                        "scene codecs reject numeric, text and array values for boolean choices"))
                {
                    return false;
                }
            }
        }
    }
    return true;
}

} // namespace

int main()
{
    return mesh_optional_fields() && collider_optional_fields() && render_policy_fields() ? 0 : 1;
}
