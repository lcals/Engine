#pragma once

#include "editor/asset_pipeline/asset_importer.hpp"

namespace engine::plugins::font_import
{

class FontImporter final : public editor::asset_pipeline::AssetImporter
{
public:
    [[nodiscard]] std::string_view id() const noexcept override;
    [[nodiscard]] schema::AssetKind kind() const noexcept override;
    [[nodiscard]] bool matches(const std::filesystem::path& source) const override;
    [[nodiscard]] std::string_view default_meta() const noexcept override;
    [[nodiscard]] bool inspect_batch(const editor::asset_pipeline::ImportContext& context,
                                     std::string& fingerprint, std::string& error) override;
    [[nodiscard]] bool convert(const editor::asset_pipeline::ImportAsset& asset,
                               const editor::asset_pipeline::ImportContext& context,
                               std::string& error) const override;
};

} // namespace engine::plugins::font_import
