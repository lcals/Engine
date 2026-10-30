#pragma once

#include "editor/asset_pipeline/asset_importer.hpp"

#include <memory>

namespace engine::plugins::fbx_import
{
// 只在编辑器读取 FBX；运行包消费 Model/Mesh/Skeleton/Animation 缓存。
class FbxImporter final : public editor::asset_pipeline::AssetImporter
{
public:
    FbxImporter();
    ~FbxImporter() noexcept override;
    FbxImporter(const FbxImporter&) = delete;
    FbxImporter& operator=(const FbxImporter&) = delete;

    [[nodiscard]] std::string_view id() const noexcept override;
    [[nodiscard]] schema::AssetKind kind() const noexcept override;
    [[nodiscard]] bool matches(const std::filesystem::path& source) const override;
    [[nodiscard]] std::string_view default_meta() const noexcept override;
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
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace engine::plugins::fbx_import
