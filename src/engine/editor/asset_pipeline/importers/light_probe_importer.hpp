#pragma once

#include "editor/asset_pipeline/asset_importer.hpp"

namespace engine::editor::asset_pipeline
{
class LightProbeImporter final : public AssetImporter
{
public:
    [[nodiscard]] std::string_view id() const noexcept override;
    [[nodiscard]] schema::AssetKind kind() const noexcept override;
    [[nodiscard]] bool matches(const std::filesystem::path& source) const override;
    [[nodiscard]] bool inspect_batch(const ImportContext& context, std::string& fingerprint,
                                     std::string& error) override;
    [[nodiscard]] bool convert(const ImportAsset& asset, const ImportContext& context,
                               std::string& error) const override;
};
} // namespace engine::editor::asset_pipeline
