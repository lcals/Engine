#pragma once

#include "editor/asset_pipeline/asset_importer.hpp"
#include "runtime/schema/mesh_generated.h"
#include "runtime/schema/model_generated.h"

#include <map>
#include <memory>

namespace engine::plugins::usd_import
{
class UsdImporter final : public editor::asset_pipeline::AssetImporter
{
public:
    [[nodiscard]] std::string_view id() const noexcept override;
    [[nodiscard]] schema::AssetKind kind() const noexcept override;
    [[nodiscard]] bool matches(const std::filesystem::path& source) const override;
    [[nodiscard]] bool discover(const editor::asset_pipeline::ImportAsset& source,
                                std::vector<editor::asset_pipeline::ImportSubresource>& output,
                                std::string& error) const override;
    [[nodiscard]] bool inspect_batch(const editor::asset_pipeline::ImportContext& context,
                                     std::string& fingerprint, std::string& error) override;
    [[nodiscard]] bool scan_dependencies(editor::asset_pipeline::ImportAsset& asset,
                                         const editor::asset_pipeline::ImportContext& context,
                                         std::string& error) const override;
    [[nodiscard]] bool convert(const editor::asset_pipeline::ImportAsset& asset,
                               const editor::asset_pipeline::ImportContext& context,
                               std::string& error) const override;

private:
    struct ParsedModel
    {
        std::string source_hash;
        schema::ModelDataT model;
        std::map<std::string, schema::MeshDataT> meshes;
        std::map<std::size_t, std::string> node_meshes;
    };
    mutable std::map<std::filesystem::path, ParsedModel> parsed_;
};
} // namespace engine::plugins::usd_import
