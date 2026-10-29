#pragma once

#include "editor/asset_pipeline/asset_importer.hpp"

namespace engine::asset_pipeline
{
class ShaderImporter final : public AssetImporter
{
public:
    [[nodiscard]] schema::AssetKind kind() const noexcept override;
    [[nodiscard]] bool matches(const std::filesystem::path& source) const override;
    [[nodiscard]] bool inspect_batch(const ImportContext& context, std::string& fingerprint,
                                     std::string& error) override;
    [[nodiscard]] bool scan_dependencies(ImportAsset& asset, const ImportContext& context,
                                         std::string& error) const override;
    [[nodiscard]] bool convert(const ImportAsset& asset, const ImportContext& context,
                               std::string& error) const override;
};
} // namespace engine::asset_pipeline
