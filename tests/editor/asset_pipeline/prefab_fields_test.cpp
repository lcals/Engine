#include "editor/asset_pipeline/prefab_importer.hpp"

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
    hidden.enabled = false;
    hidden.visible = false;
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

} // namespace

int main()
{
    return mesh_optional_fields() && collider_optional_fields() ? 0 : 1;
}
