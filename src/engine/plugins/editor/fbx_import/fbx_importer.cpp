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

struct FbxImporter::State
{
    std::map<std::filesystem::path, FbxDocument> documents;
};

namespace
{
using Documents = std::map<std::filesystem::path, FbxDocument>;

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
                       const ImportAsset& asset, const ImportContext& context, const Guid& material,
                       schema::ModelDataT& model, std::string& error)
{
    auto result = std::make_unique<schema::ModelNodeDataT>();
    const auto& path = document.node_paths.at(&node);
    result->name = path.substr(path.find_last_of('/') + 1);
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
        result->material_guids.assign(std::max<std::size_t>(1, node.materials.count), material);
    }
    const auto index = static_cast<std::int32_t>(model.nodes.size());
    model.nodes.push_back(std::move(result));
    for (const auto* child : node.children)
    {
        if (!append_model_node(document, *child, index, asset, context, material, model, error))
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
    return "sample_rate = 30\nskeleton = \"\"\nnormals = \"import\"\n";
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
    for (const auto& [source, document] : state_->documents)
    {
        std::string hash;
        auto meta = source;
        meta += ".meta";
        toml::table settings;
        if (!file_hash(source, hash, error) || hash != document.source_hash ||
            !read_table(meta, settings, error) ||
            (settings.contains("sample_rate") && !settings["sample_rate"].is_number()) ||
            (settings.contains("skeleton") && !settings["skeleton"].is_string()) ||
            settings["sample_rate"].value_or(30.0) != document.sample_rate ||
            settings["skeleton"].value_or(std::string{}) != document.external_skeleton ||
            (settings.contains("normals") && !settings["normals"].is_string()) ||
            settings["normals"].value_or(std::string{"import"}) != document.normals_mode)
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
    fingerprint += ":ufbx-0.23.1:fbx-importer-2";
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
        for (const auto* child : context.sources)
        {
            if (child->parent_guid == asset.guid && !add(child->guid, child->kind))
            {
                return false;
            }
        }
        if (!skeleton_guid.empty() && !add(skeleton_guid, schema::AssetKind::skeleton))
        {
            return false;
        }
        if (!document->second.meshes.empty())
        {
            Guid material;
            if (!context.identities.path_to_guid("builtin/materials/default-pbr", material) ||
                !add(material, schema::AssetKind::material))
            {
                error = "Built-in default PBR material is unavailable";
                return false;
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
        model.path = asset.path;
        model.skeleton_guid = skeleton_guid;
        Guid material;
        if (!document.meshes.empty() &&
            !context.identities.path_to_guid("builtin/materials/default-pbr", material))
        {
            error = "Built-in PBR material identity is missing";
            return false;
        }
        if (!append_model_node(document, *document.scene->root_node, -1, asset, context, material,
                               model, error))
        {
            return false;
        }
        for (const auto& [key, stack] : document.animations)
        {
            auto animation = std::make_unique<schema::ModelAnimationDataT>();
            animation->name.assign(stack->name.data, stack->name.length);
            if (animation->name.empty())
            {
                animation->name = "unnamed";
            }
            if (!context.identities.path_to_guid(asset.path + "/" + key, animation->guid))
            {
                error = "FBX animation child identity is missing";
                return false;
            }
            model.animations.push_back(std::move(animation));
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
