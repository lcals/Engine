#include "plugins/editor/usd_import/usd_importer.hpp"

#include "editor/asset_pipeline/asset_io.hpp"
#include "editor/asset_pipeline/importer_utils.hpp"
#include "runtime/resource/codecs/mesh_resource_codec.hpp"
#include "runtime/resource/codecs/model_resource_codec.hpp"
#include "runtime/utils/path_utils.hpp"

#include <pxr/base/gf/vec3d.h>
#include <pxr/base/tf/errorMark.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/xformCache.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace engine::plugins::usd_import
{
namespace
{
constexpr std::string_view default_material = "00000000-0000-4000-8000-000000000003";

bool compile_mesh(const pxr::UsdGeomMesh& source, pxr::UsdGeomXformCache& transforms, double meters,
                  bool z_up, schema::MeshDataT& mesh, std::string& error)
{
    pxr::VtArray<pxr::GfVec3f> points;
    pxr::VtArray<int> counts;
    pxr::VtArray<int> indices;
    pxr::VtArray<int> holes;
    if (!source.GetPointsAttr().Get(&points) || !source.GetFaceVertexCountsAttr().Get(&counts) ||
        !source.GetFaceVertexIndicesAttr().Get(&indices) || points.empty() ||
        source.GetPointsAttr().ValueMightBeTimeVarying() ||
        source.GetFaceVertexCountsAttr().ValueMightBeTimeVarying() ||
        source.GetFaceVertexIndicesAttr().ValueMightBeTimeVarying() ||
        source.GetPrim().HasRelationship(pxr::TfToken("skel:skeleton")))
    {
        error = "USD importer requires static polygon mesh data";
        return false;
    }
    if (source.GetHoleIndicesAttr().Get(&holes) && !holes.empty())
    {
        error = "USD mesh holes are not supported";
        return false;
    }
    pxr::TfToken subdivision;
    if (!source.GetSubdivisionSchemeAttr().Get(&subdivision) ||
        subdivision != pxr::UsdGeomTokens->none)
    {
        error = "USD subdivision surfaces require conversion to polygon meshes before import";
        return false;
    }
    if (indices.size() > std::numeric_limits<std::uint32_t>::max())
    {
        error = "USD polygon mesh exceeds the runtime index limit";
        return false;
    }
    const auto matrix = transforms.GetLocalToWorldTransform(source.GetPrim());
    std::vector<pxr::GfVec3d> positions;
    for (const auto& point : points)
    {
        auto world = matrix.Transform(pxr::GfVec3d(point)) * meters;
        if (z_up)
        {
            world = {world[0], world[2], -world[1]};
        }
        if (!std::isfinite(world[0]) || !std::isfinite(world[1]) || !std::isfinite(world[2]))
        {
            error = "USD mesh has non-finite positions";
            return false;
        }
        positions.push_back(world);
    }
    pxr::TfToken orientation;
    if (!source.GetOrientationAttr().Get(&orientation) ||
        (orientation != pxr::UsdGeomTokens->leftHanded &&
         orientation != pxr::UsdGeomTokens->rightHanded))
    {
        error = "USD mesh has an unsupported orientation";
        return false;
    }
    const bool reverse =
        (orientation == pxr::UsdGeomTokens->leftHanded) != (matrix.GetDeterminant() < 0);
    std::size_t offset = 0;
    for (int count : counts)
    {
        if (count < 3 || offset > indices.size() ||
            static_cast<std::size_t>(count) > indices.size() - offset)
        {
            error = "USD mesh contains invalid polygon topology";
            return false;
        }
        for (int index = 0; index < count; ++index)
        {
            const auto vertex = indices[offset + static_cast<std::size_t>(index)];
            if (vertex < 0 || static_cast<std::size_t>(vertex) >= positions.size())
            {
                error = "USD polygon vertex is out of range";
                return false;
            }
        }
        const auto& a = positions[static_cast<std::size_t>(indices[offset])];
        const auto& b = positions[static_cast<std::size_t>(indices[offset + 1])];
        const auto& c = positions[static_cast<std::size_t>(indices[offset + 2])];
        auto normal = pxr::GfCross(b - a, c - a);
        if (normal.Normalize() < 1.0e-12)
        {
            error = "USD polygon is degenerate";
            return false;
        }
        // 扇形仅用于凸且共面的多边形；凹面必须先在源工具三角化。
        for (int index = 0; index < count; ++index)
        {
            const auto& p = positions[static_cast<std::size_t>(indices[offset + index])];
            const auto& q =
                positions[static_cast<std::size_t>(indices[offset + (index + 1) % count])];
            if (std::abs(pxr::GfDot(p - a, normal)) > 1.0e-5 || (q - p).GetLengthSq() < 1.0e-20)
            {
                error = "USD non-planar or degenerate polygon requires source triangulation";
                return false;
            }
            // 每条边的所有顶点必须在同一半平面，排除凹面和自交星形。
            for (int other = 0; other < count; ++other)
            {
                const auto& r = positions[static_cast<std::size_t>(indices[offset + other])];
                if (pxr::GfDot(pxr::GfCross(q - p, r - p), normal) < -1.0e-10)
                {
                    error =
                        "USD concave or self-intersecting polygon requires source triangulation";
                    return false;
                }
            }
        }
        if (reverse)
        {
            normal = -normal;
        }
        if (static_cast<std::size_t>(count) >
                std::numeric_limits<std::uint32_t>::max() - mesh.vertices.size() ||
            static_cast<std::size_t>(count - 2) >
                (std::numeric_limits<std::uint32_t>::max() - mesh.indices.size()) / 3)
        {
            error = "USD triangulation exceeds the runtime mesh limits";
            return false;
        }
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        for (int index = 0; index < count; ++index)
        {
            const auto& p = positions[static_cast<std::size_t>(indices[offset + index])];
            mesh.vertices.emplace_back(static_cast<float>(p[0]), static_cast<float>(p[1]),
                                       static_cast<float>(p[2]), static_cast<float>(normal[0]),
                                       static_cast<float>(normal[1]),
                                       static_cast<float>(normal[2]));
        }
        for (int index = 1; index + 1 < count; ++index)
        {
            mesh.indices.push_back(base);
            mesh.indices.push_back(base + static_cast<std::uint32_t>(reverse ? index + 1 : index));
            mesh.indices.push_back(base + static_cast<std::uint32_t>(reverse ? index : index + 1));
        }
        offset += static_cast<std::size_t>(count);
    }
    if (offset != indices.size())
    {
        error = "USD face counts do not match indices";
        return false;
    }
    return MeshResourceCodec::validate(mesh);
}
} // namespace
std::string_view UsdImporter::id() const noexcept
{
    return "usd";
}
schema::AssetKind UsdImporter::kind() const noexcept
{
    return schema::AssetKind::model;
}
bool UsdImporter::matches(const std::filesystem::path& source) const
{
    const auto ext = editor::asset_pipeline::source_extension(source);
    return ext == ".usd" || ext == ".usda" || ext == ".usdc";
}
bool UsdImporter::inspect_batch(const editor::asset_pipeline::ImportContext& context,
                                std::string& fingerprint, std::string& error)
{
    std::set<std::filesystem::path> current;
    for (const auto* source : context.sources)
    {
        if (source->importer_id == id())
        {
            current.insert(source->source);
        }
    }
    std::erase_if(parsed_, [&](const auto& entry) { return !current.contains(entry.first); });
    for (const auto& [path, model] : parsed_)
    {
        std::string hash;
        if (!editor::asset_pipeline::file_hash(path, hash, error) || hash != model.source_hash)
        {
            error = "USD source changed during import; retry the build";
            return false;
        }
    }
    if (!editor::asset_pipeline::schema_fingerprint({"model.fbs", "mesh.fbs"}, fingerprint, error))
    {
        return false;
    }
    fingerprint += ":usd-static-3:" + std::to_string(PXR_VERSION);
    return true;
}
bool UsdImporter::discover(const editor::asset_pipeline::ImportAsset& source,
                           std::vector<editor::asset_pipeline::ImportSubresource>& output,
                           std::string& error) const
{
    output.clear();
    parsed_.erase(source.source);
    ParsedModel parsed;
    if (!editor::asset_pipeline::file_hash(source.source, parsed.source_hash, error))
    {
        return false;
    }
    // 独立读取避免其他编辑器持有的 SdfLayer 缓存影响本批快照。
    pxr::TfErrorMark diagnostics;
    const auto layer = pxr::SdfLayer::OpenAsAnonymous(PathUtils::to_utf8(source.source));
    if (!layer)
    {
        diagnostics.Clear();
        error = "Cannot open USD source layer";
        return false;
    }
    // 在 Stage 组合之前拒绝外部依赖，包括缺失或未激活的引用。
    if (!layer->GetCompositionAssetDependencies().empty() ||
        !layer->GetExternalAssetDependencies().empty())
    {
        error = "USD importer requires a self-contained source; external layers, references and "
                "payloads are unsupported";
        return false;
    }
    const auto stage = pxr::UsdStage::Open(layer, pxr::UsdStage::LoadNone);
    if (!stage)
    {
        diagnostics.Clear();
        error = "Cannot compose USD source stage";
        return false;
    }
    const auto session_layer = stage->GetSessionLayer();
    for (const auto& used : stage->GetUsedLayers())
    {
        // OpenUSD 25.8 的强/弱句柄混合比较在 C++20 下会递归；比较借用对象身份。
        if (used.operator->() != layer.operator->() &&
            used.operator->() != session_layer.operator->())
        {
            error = "USD source composed an untracked external layer";
            return false;
        }
    }
    parsed.model.schema_version = 2;
    parsed.model.path = source.path;
    std::map<pxr::SdfPath, int> node_indices;
    pxr::UsdGeomXformCache transforms;
    const double meters = pxr::UsdGeomGetStageMetersPerUnit(stage);
    const auto up_axis = pxr::UsdGeomGetStageUpAxis(stage);
    const bool z_up = up_axis == pxr::UsdGeomTokens->z;
    if (!std::isfinite(meters) || meters <= 0 || (up_axis != pxr::UsdGeomTokens->y && !z_up))
    {
        error = "USD stage requires a positive unit scale and Y-up or Z-up coordinates";
        return false;
    }
    for (const auto& prim : stage->Traverse())
    {
        if (prim.IsInstance() || prim.GetTypeName() == pxr::TfToken("SkelRoot") ||
            pxr::UsdGeomXformable(prim).TransformMightBeTimeVarying())
        {
            error = "USD animated transforms, skeletons and instances are not yet supported";
            return false;
        }
        const auto type = prim.GetTypeName();
        if (!type.IsEmpty() && type != pxr::TfToken("Xform") && type != pxr::TfToken("Scope") &&
            !prim.IsA<pxr::UsdGeomMesh>())
        {
            error = "Unsupported USD primitive type: " + type.GetString() + " at " +
                    prim.GetPath().GetString();
            return false;
        }
        for (const auto& attribute : prim.GetAuthoredAttributes())
        {
            if (attribute.ValueMightBeTimeVarying())
            {
                error = "USD time-varying mesh attributes require an animation importer";
                return false;
            }
        }
        auto node = std::make_unique<schema::ModelNodeDataT>();
        node->name = prim.GetName().GetString();
        const auto parent = node_indices.find(prim.GetParent().GetPath());
        node->parent = parent == node_indices.end() ? -1 : parent->second;
        const auto index = parsed.model.nodes.size();
        node_indices.emplace(prim.GetPath(), static_cast<int>(index));
        if (prim.IsA<pxr::UsdGeomMesh>())
        {
            const auto key = "meshes" + prim.GetPath().GetString();
            schema::MeshDataT mesh;
            mesh.path = source.path + "/" + key;
            if (!compile_mesh(pxr::UsdGeomMesh(prim), transforms, meters, z_up, mesh, error))
            {
                if (error.empty())
                {
                    error = "USD mesh failed validation";
                }
                return false;
            }
            parsed.meshes.emplace(key, std::move(mesh));
            parsed.node_meshes.emplace(index, key);
            output.push_back({key, schema::AssetKind::mesh});
        }
        parsed.model.nodes.push_back(std::move(node));
    }
    if (parsed.meshes.empty())
    {
        error = "USD stage has no supported polygon meshes";
        return false;
    }
    if (!diagnostics.IsClean())
    {
        diagnostics.Clear();
        error = "USD composition or geometry access reported errors";
        return false;
    }
    std::string current_hash;
    if (!editor::asset_pipeline::file_hash(source.source, current_hash, error) ||
        current_hash != parsed.source_hash)
    {
        error = "USD source changed during discovery; retry the build";
        return false;
    }
    parsed_.emplace(source.source, std::move(parsed));
    return true;
}
bool UsdImporter::scan_dependencies(editor::asset_pipeline::ImportAsset& asset,
                                    const editor::asset_pipeline::ImportContext& context,
                                    std::string& error) const
{
    asset.build_dependencies.clear();
    asset.runtime_dependencies.clear();
    const auto item = parsed_.find(asset.source);
    if (item == parsed_.end())
    {
        error = "USD source was not discovered";
        return false;
    }
    if (!asset.subresource_key.empty())
    {
        return true;
    }
    for (const auto& [key, mesh] : item->second.meshes)
    {
        (void)mesh;
        Guid guid;
        if (!context.identities.path_to_guid(asset.path + "/" + key, guid))
        {
            error = "USD mesh subresource identity missing";
            return false;
        }
        asset.build_dependencies.push_back(guid);
    }
    asset.build_dependencies.emplace_back(default_material);
    asset.runtime_dependencies = asset.build_dependencies;
    return true;
}
bool UsdImporter::convert(const editor::asset_pipeline::ImportAsset& asset,
                          const editor::asset_pipeline::ImportContext& context,
                          std::string& error) const
{
    const auto found = parsed_.find(asset.source);
    if (found == parsed_.end())
    {
        error = "USD source was not discovered before conversion";
        return false;
    }
    flatbuffers::FlatBufferBuilder builder;
    if (asset.subresource_key.empty())
    {
        auto model = found->second.model;
        for (const auto& [index, key] : found->second.node_meshes)
        {
            if (!context.identities.path_to_guid(asset.path + "/" + key,
                                                 model.nodes[index]->mesh_guid))
            {
                error = "USD mesh GUID missing";
                return false;
            }
            model.nodes[index]->material_guids.emplace_back(default_material);
        }
        if (!ModelResourceCodec::validate(model))
        {
            error = "Generated USD model is invalid";
            return false;
        }
        schema::FinishModelDataBuffer(builder, schema::ModelData::Pack(builder, &model));
    }
    else
    {
        const auto mesh = found->second.meshes.find(asset.subresource_key);
        if (mesh == found->second.meshes.end())
        {
            error = "USD mesh child key is missing";
            return false;
        }
        schema::FinishMeshDataBuffer(builder, schema::MeshData::Pack(builder, &mesh->second));
    }
    return editor::asset_pipeline::write_bytes(
        context.output / asset.guid,
        std::as_bytes(std::span(builder.GetBufferPointer(), builder.GetSize())), error);
}
} // namespace engine::plugins::usd_import
