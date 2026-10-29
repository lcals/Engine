#pragma once

#include "editor/asset_pipeline/importers/mesh_importer.hpp"

namespace engine::plugins::modeling
{
class ModelingMeshImporter final : public editor::asset_pipeline::MeshImporter
{
public:
    [[nodiscard]] bool inspect_batch(const editor::asset_pipeline::ImportContext& context,
                                     std::string& fingerprint, std::string& error) override;
    [[nodiscard]] bool convert(const editor::asset_pipeline::ImportAsset& asset,
                               const editor::asset_pipeline::ImportContext& context,
                               std::string& error) const override;
};
} // namespace engine::plugins::modeling
