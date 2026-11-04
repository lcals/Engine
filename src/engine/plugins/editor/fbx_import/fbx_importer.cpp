#include "plugins/editor/fbx_import/fbx_importer.hpp"

#include "editor/asset_pipeline/asset_io.hpp"
#include "editor/asset_pipeline/importer_utils.hpp"
#include "plugins/editor/fbx_import/fbx_document.hpp"
#include "runtime/resource/codecs/animation_resource_codec.hpp"
#include "runtime/resource/codecs/mesh_resource_codec.hpp"
#include "runtime/resource/codecs/model_resource_codec.hpp"
#include "runtime/resource/codecs/skeleton_resource_codec.hpp"

#include <algorithm>
#include <flatbuffers/flatbuffers.h>
#include <map>
#include <set>
#include <span>

namespace engine::plugins::fbx_import
{
using namespace editor::asset_pipeline;

namespace
{
using MaterialRemaps = std::map<std::string, std::vector<std::string>>;
}

struct FbxImporter::State
{
    std::map<std::filesystem::path, FbxDocument> documents;
    std::map<std::filesystem::path, MaterialRemaps> material_remaps;
};

namespace
{
using Documents = std::map<std::filesystem::path, FbxDocument>;

bool read_material_remaps(const toml::table& settings, const FbxDocument& document,
                          MaterialRemaps& output, std::string& error)
{
    output.clear();
    if (!settings.contains("material_remaps"))
    {
        return true;
    }
    const auto* entries = settings["material_remaps"].as_array();
    if (entries == nullptr)
    {
        error = "FBX material_remaps must be an array of tables";
        return false;
    }
    MaterialRemaps remaps;
    for (const auto& entry : *entries)
    {
        const auto* table = entry.as_table();
        if (table == nullptr || table->size() != 2 || !(*table)["mesh"].is_string())
        {
            error = "FBX material remap must contain only mesh and materials";
            return false;
        }
        const auto key = (*table)["mesh"].value_or(std::string{});
        const auto mesh = document.meshes.find(key);
        if (mesh == document.meshes.end() || remaps.contains(key))
        {
            error = "FBX material remap mesh is unknown or repeated: " + key;
            return false;
        }
        const auto* paths = (*table)["materials"].as_array();
        if (paths == nullptr ||
            paths->size() != std::max<std::size_t>(1, mesh->second->materials.count))
        {
            error = "FBX material remap must fill every material slot: " + key;
            return false;
        }
        std::vector<std::string> materials;
        for (const auto& path : *paths)
        {
            const auto value = path.value<std::string>();
            if (!value || value->empty())
            {
                error = "FBX material remap paths must be nonempty strings: " + key;
                return false;
            }
            materials.push_back(*value);
        }
        remaps.emplace(key, std::move(materials));
    }
    output.swap(remaps);
    return true;
}

bool resolve_materials(const std::string& key, const ufbx_node& node, const MaterialRemaps& remaps,
                       const ImportContext& context, std::vector<Guid>& output, std::string& error)
{
    output.clear();
    std::vector<Guid> materials;
    const auto found = remaps.find(key);
    const auto count = std::max<std::size_t>(1, node.materials.count);
    for (std::size_t slot = 0; slot < count; ++slot)
    {
        const std::string path =
            found == remaps.end() ? "builtin://materials/default-pbr" : found->second[slot];
        Guid guid;
        if (!context.identities.path_to_guid(path, guid))
        {
            error = "FBX material path does not exist: " + path;
            return false;
        }
        const auto kind = context.kinds.find(guid);
        if (kind == context.kinds.end() || kind->second != schema::AssetKind::material)
        {
            error = "FBX material path does not identify a Material: " + path;
            return false;
        }
        materials.push_back(std::move(guid));
    }
    output.swap(materials);
    return true;
}

bool resolve_skeleton(const ImportAsset& asset, const ImportContext& context,
                      const Documents& documents, const FbxDocument*& output, Guid& guid,
                      std::string& error)
{
    output = nullptr;
    guid.clear();
    const auto found = documents.find(asset.source);
    if (found == documents.end())
    {
        error = "FBX source was not discovered before conversion";
        return false;
    }
    const auto& document = found->second;
    if (document.joints.empty() && document.external_skeleton.empty())
    {
        return true;
    }
    std::string parent_path;
    if (!context.identities.guid_to_path(asset.parent_guid.empty() ? asset.guid : asset.parent_guid,
                                         parent_path))
    {
        error = "FBX parent identity is missing";
        return false;
    }
    const auto path =
        document.external_skeleton.empty() ? parent_path + "/skeleton" : document.external_skeleton;
    if (!context.identities.path_to_guid(path, guid))
    {
        error = "FBX skeleton path does not exist: " + path;
        return false;
    }
    for (const auto* source : context.sources)
    {
        if (source->guid != guid)
        {
            continue;
        }
        const auto target = documents.find(source->source);
        if (source->importer_id != "fbx" || source->kind != schema::AssetKind::skeleton ||
            source->subresource_key != "skeleton" || target == documents.end() ||
            !target->second.external_skeleton.empty() || target->second.joints.empty())
        {
            error = "FBX skeleton mapping must reference a generated FBX skeleton";
            return false;
        }
        if (target->second.scale_factor != document.scale_factor ||
            target->second.rotation_offset != document.rotation_offset)
        {
            error = "FBX source and skeleton must use matching scale_factor and rotation_offset";
            return false;
        }
        output = &target->second;
        return true;
    }
    error = "FBX skeleton is absent from the current import batch";
    return false;
}

bool write_buffer(const ImportAsset& asset, const ImportContext& context,
                  const flatbuffers::FlatBufferBuilder& builder, std::string& error)
{
    return write_bytes(context.output / asset.guid,
                       std::as_bytes(std::span(builder.GetBufferPointer(), builder.GetSize())),
                       error);
}

bool append_model_node(const FbxDocument& document, const ufbx_node& node, std::int32_t parent,
                       const ImportAsset& asset, const ImportContext& context,
                       const MaterialRemaps& remaps, schema::ModelDataT& model, std::string& error)
{
    auto result = std::make_unique<schema::ModelNodeDataT>();
    const auto& path = document.node_paths.at(&node);
    result->name = path.substr(path.find_last_of('/') + 1);
    result->source_key = path;
    result->parent = parent;
    const auto& transform = node.local_transform;
    const auto euler = ufbx_quat_to_euler(transform.rotation, UFBX_ROTATION_ORDER_XYZ);
    result->position = {transform.translation.x, transform.translation.y, transform.translation.z};
    result->rotation = {euler.x, euler.y, euler.z};
    result->scale = {transform.scale.x, transform.scale.y, transform.scale.z};
    if (node.mesh != nullptr)
    {
        if (!context.identities.path_to_guid(asset.path + "/meshes/" + path, result->mesh_guid))
        {
            error = "FBX mesh child identity is missing";
            return false;
        }
        if (!resolve_materials("meshes/" + path, node, remaps, context, result->material_guids,
                               error))
        {
            return false;
        }
    }
    const auto index = static_cast<std::int32_t>(model.nodes.size());
    model.nodes.push_back(std::move(result));
    for (const auto* child : node.children)
    {
        if (!append_model_node(document, *child, index, asset, context, remaps, model, error))
        {
            return false;
        }
    }
    return true;
}
} // namespace

FbxImporter::FbxImporter() : state_(std::make_unique<State>())
{
}
FbxImporter::~FbxImporter() noexcept = default;

std::string_view FbxImporter::id() const noexcept
{
    return "fbx";
}
schema::AssetKind FbxImporter::kind() const noexcept
{
    return schema::AssetKind::model;
}
bool FbxImporter::matches(const std::filesystem::path& source) const
{
    return source_extension(source) == ".fbx";
}
std::string_view FbxImporter::default_meta() const noexcept
{
    return "sample_rate = 30\nskeleton = \"\"\nnormals = \"import\"\n"
           "scale_factor = 1.0\nrotation_offset = [0.0, 0.0, 0.0]\n";
}
bool FbxImporter::discover(const ImportAsset& source, std::vector<ImportSubresource>& output,
                           std::string& error) const
{
    output.clear();
    FbxDocument document;
    if (!load_document(source.source, document, error))
    {
        return false;
    }
    auto meta = source.source;
    meta += ".meta";
    toml::table settings;
    MaterialRemaps remaps;
    if (!read_table(meta, settings, error) ||
        !read_material_remaps(settings, document, remaps, error))
    {
        return false;
    }
    for (const auto& [key, node] : document.meshes)
    {
        (void)node;
        output.push_back({key, schema::AssetKind::mesh});
    }
    if (!document.joints.empty() && document.external_skeleton.empty())
    {
        output.push_back({"skeleton", schema::AssetKind::skeleton});
    }
    for (const auto& [key, stack] : document.animations)
    {
        (void)stack;
        output.push_back({key, schema::AssetKind::animation});
    }
    state_->documents.insert_or_assign(source.source, std::move(document));
    state_->material_remaps.insert_or_assign(source.source, std::move(remaps));
    return true;
}
bool FbxImporter::inspect_batch(const ImportContext& context, std::string& fingerprint,
                                std::string& error)
{
    std::set<std::filesystem::path> current;
    for (const auto* source : context.sources)
    {
        if (source->importer_id == id())
        {
            current.insert(source->source);
        }
    }
    std::erase_if(state_->documents,
                  [&](const auto& entry) { return !current.contains(entry.first); });
    std::erase_if(state_->material_remaps,
                  [&](const auto& entry) { return !current.contains(entry.first); });
    for (const auto& [source, document] : state_->documents)
    {
        std::string hash;
        auto meta = source;
        meta += ".meta";
        toml::table settings;
        MaterialRemaps remaps;
        if (!file_hash(source, hash, error) || hash != document.source_hash ||
            !document_settings_match(source, document, error) ||
            !read_table(meta, settings, error) ||
            !read_material_remaps(settings, document, remaps, error) ||
            remaps != state_->material_remaps.at(source))
        {
            error = "FBX source or settings changed during import; retry the build";
            return false;
        }
    }
    if (!schema_fingerprint(
            {"common.fbs", "model.fbs", "mesh.fbs", "skeleton.fbs", "animation.fbs"}, fingerprint,
            error))
    {
        return false;
    }
    fingerprint +=
        ":ufbx-0.23.1:ozz-0.16.0:mikktspace-3e895b49-angle-precision-1:fbx-importer-12";
    return true;
}
bool FbxImporter::scan_dependencies(ImportAsset& asset, const ImportContext& context,
                                    std::string& error) const
{
    asset.build_dependencies.clear();
    asset.runtime_dependencies.clear();
    const auto document = state_->documents.find(asset.source);
    if (document == state_->documents.end())
    {
        error = "FBX document is missing";
        return false;
    }
    const FbxDocument* skeleton = nullptr;
    Guid skeleton_guid;
    if (!resolve_skeleton(asset, context, state_->documents, skeleton, skeleton_guid, error))
    {
        return false;
    }
    const auto add = [&](const Guid& guid, schema::AssetKind kind)
    { return reference(guid, kind, context, asset.runtime_dependencies, error); };
    if (asset.subresource_key.empty())
    {
        for (const auto& [key, node] : document->second.meshes)
        {
            Guid mesh_guid;
            if (!context.identities.path_to_guid(asset.path + "/" + key, mesh_guid) ||
                !add(mesh_guid, schema::AssetKind::mesh))
            {
                error = "FBX model mesh identity is unavailable";
                return false;
            }
            std::vector<Guid> materials;
            if (!resolve_materials(key, *node, state_->material_remaps.at(asset.source), context,
                                   materials, error))
            {
                return false;
            }
            for (const auto& material : materials)
            {
                if (!add(material, schema::AssetKind::material))
                {
                    return false;
                }
            }
        }
    }
    else if (asset.kind == schema::AssetKind::animation ||
             (asset.kind == schema::AssetKind::mesh &&
              document->second.meshes.at(asset.subresource_key)->mesh->skin_deformers.count != 0))
    {
        if (skeleton_guid.empty() || !add(skeleton_guid, schema::AssetKind::skeleton))
        {
            error = "FBX skin or animation requires a skeleton";
            return false;
        }
    }
    std::sort(asset.runtime_dependencies.begin(), asset.runtime_dependencies.end());
    asset.runtime_dependencies.erase(
        std::unique(asset.runtime_dependencies.begin(), asset.runtime_dependencies.end()),
        asset.runtime_dependencies.end());
    asset.build_dependencies = asset.runtime_dependencies;
    return true;
}
bool FbxImporter::convert(const ImportAsset& asset, const ImportContext& context,
                          std::string& error) const
{
    const auto found = state_->documents.find(asset.source);
    if (found == state_->documents.end())
    {
        error = "FBX document is absent during conversion";
        return false;
    }
    const auto& document = found->second;
    const FbxDocument* skeleton = nullptr;
    Guid skeleton_guid;
    if (!resolve_skeleton(asset, context, state_->documents, skeleton, skeleton_guid, error))
    {
        return false;
    }
    flatbuffers::FlatBufferBuilder builder;
    if (asset.subresource_key.empty())
    {
        schema::ModelDataT model;
        model.schema_version = 2;
        model.path = asset.path;
        if (!append_model_node(document, *document.scene->root_node, -1, asset, context,
                               state_->material_remaps.at(asset.source), model, error))
        {
            return false;
        }
        if (!ModelResourceCodec::validate(model))
        {
            error = "Generated FBX object hierarchy is invalid";
            return false;
        }
        schema::FinishModelDataBuffer(builder, schema::ModelData::Pack(builder, &model));
    }
    else if (asset.kind == schema::AssetKind::skeleton)
    {
        schema::SkeletonDataT result;
        result.path = asset.path;
        if (!convert_skeleton(document, result, error) || !SkeletonResourceCodec::validate(result))
        {
            if (error.empty())
            {
                error = "Generated FBX skeleton is invalid";
            }
            return false;
        }
        schema::FinishSkeletonDataBuffer(builder, schema::SkeletonData::Pack(builder, &result));
    }
    else if (asset.kind == schema::AssetKind::mesh)
    {
        schema::MeshDataT result;
        result.path = asset.path;
        const auto mesh = document.meshes.find(asset.subresource_key);
        if (mesh == document.meshes.end())
        {
            error = "FBX mesh key no longer exists";
            return false;
        }
        if (mesh->second->mesh->skin_deformers.count != 0)
        {
            result.skeleton_guid = skeleton_guid;
        }
        if (!convert_mesh(document, *mesh->second, result, error) ||
            !MeshResourceCodec::validate(result))
        {
            if (error.empty())
            {
                error = "Generated FBX mesh is invalid";
            }
            return false;
        }
        schema::FinishMeshDataBuffer(builder, schema::MeshData::Pack(builder, &result));
    }
    else if (asset.kind == schema::AssetKind::animation)
    {
        schema::AnimationDataT result;
        result.path = asset.path;
        result.skeleton_guid = skeleton_guid;
        const auto animation = document.animations.find(asset.subresource_key);
        if (skeleton == nullptr || animation == document.animations.end() ||
            !convert_animation(document, *skeleton, *animation->second, result, error) ||
            !AnimationResourceCodec::validate(result))
        {
            if (error.empty())
            {
                error = "Generated FBX animation is invalid";
            }
            return false;
        }
        schema::FinishAnimationDataBuffer(builder, schema::AnimationData::Pack(builder, &result));
    }
    else
    {
        error = "Unsupported FBX output kind";
        return false;
    }
    return write_buffer(asset, context, builder, error);
}
} // namespace engine::plugins::fbx_import
