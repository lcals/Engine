#pragma once

#include "editor/asset_pipeline/asset_importer.hpp"

namespace engine::editor::asset_pipeline
{
class TextureImporter final : public AssetImporter
{
public:
    [[nodiscard]] schema::AssetKind kind() const noexcept override;
    [[nodiscard]] bool matches(const std::filesystem::path& source) const override;
    [[nodiscard]] bool inspect_batch(const ImportContext& context, std::string& fingerprint,
                                     std::string& error) override;
    [[nodiscard]] bool prepare_batch(const ImportContext& context, TaskDescription& task,
                                     std::string& error) const override;
    [[nodiscard]] std::string_view default_meta() const noexcept override;
    [[nodiscard]] static std::string_view version() noexcept;
    [[nodiscard]] static bool initialize(std::string& error);
    [[nodiscard]] bool convert(const ImportAsset& asset, const ImportContext& context,
                               std::string& error) const override;
};
} // namespace engine::editor::asset_pipeline
