#pragma once

#include "editor/asset_pipeline/import_context.hpp"
#include "editor/asset_pipeline/import_subresource.hpp"
#include "editor/task/task_system.hpp"

#include <string_view>

namespace engine::editor::asset_pipeline
{
class AssetImporter
{
public:
    virtual ~AssetImporter() noexcept = default;
    [[nodiscard]] virtual std::string_view id() const noexcept;
    // 协调线程枚举稳定子资源；只描述身份，不写缓存。
    [[nodiscard]] virtual bool discover(const ImportAsset& source,
                                        std::vector<ImportSubresource>& output,
                                        std::string& error) const;
    [[nodiscard]] virtual schema::AssetKind kind() const noexcept = 0;
    [[nodiscard]] virtual bool matches(const std::filesystem::path& source) const = 0;
    [[nodiscard]] virtual std::string_view default_meta() const noexcept;
    [[nodiscard]] virtual std::string module_path(const std::filesystem::path& relative) const;
    [[nodiscard]] virtual bool inspect_batch(const ImportContext& context, std::string& fingerprint,
                                             std::string& error);
    [[nodiscard]] virtual bool scan_dependencies(ImportAsset& asset, const ImportContext& context,
                                                 std::string& error) const;
    // 协调线程调用；可返回一个公共前置任务，error须存活到任务结束。
    [[nodiscard]] virtual bool prepare_batch(const ImportContext& context, TaskDescription& task,
                                             std::string& error) const;
    [[nodiscard]] virtual bool convert(const ImportAsset& asset, const ImportContext& context,
                                       std::string& error) const = 0;
};
} // namespace engine::editor::asset_pipeline
