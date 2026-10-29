#pragma once

#include "editor/asset_pipeline/asset_importer.hpp"

#include <memory>
#include <vector>

namespace engine::asset_pipeline
{
class AssetImporterRegistry final
{
public:
    [[nodiscard]] bool initialize();
    template <typename T> [[nodiscard]] bool add(bool fallback = false)
    {
        std::unique_ptr<AssetImporter> importer = std::make_unique<T>();
        return add(importer, fallback);
    }
    // 成功才转移所有权；重复类型、空实现和第二个fallback返回失败。
    [[nodiscard]] bool add(std::unique_ptr<AssetImporter>& importer, bool fallback = false);
    [[nodiscard]] AssetImporter* find(schema::AssetKind kind) const noexcept;
    [[nodiscard]] AssetImporter* select(const std::filesystem::path& source) const;

private:
    std::vector<std::unique_ptr<AssetImporter>> importers_;
    AssetImporter* fallback_ = nullptr;
};
} // namespace engine::asset_pipeline
