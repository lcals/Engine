#pragma once

#include "runtime/schema/asset_catalog_generated.h"
#include "runtime/system/system.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace engine
{

class TaskSystem;

struct AssetBuildStats
{
    std::size_t scanned = 0;
    std::size_t imported = 0;
    std::size_t reused = 0;
    std::size_t removed = 0;
    std::size_t import_threads = 4;
    std::string revision;
};

class AssetPipelineSystem final : public System
{
public:
    AssetPipelineSystem();
    ~AssetPipelineSystem() noexcept override;

    // target_platform 为空采用宿主；支持 macos/ios/windows/android/linux。
    [[nodiscard]] bool build_assets(const std::filesystem::path& project_root,
                                    std::string_view target_platform = {});
    [[nodiscard]] bool build_scripts(const std::filesystem::path& project_root);
    // 失败保留上一次成功批次统计；所有接口由编辑器协调线程调用。
    [[nodiscard]] const AssetBuildStats& last_build() const noexcept;
    [[nodiscard]] const std::string& last_error() const noexcept;
    // 最近成功导入的完整项目目录；不包含资源实体，下一次成功导入后更新。
    [[nodiscard]] const schema::AssetCatalogDataT& catalog() const noexcept;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    void on_shutdown() noexcept override;

private:
    TaskSystem* tasks_ = nullptr; // System 子系统树独占，本指针借用。
    AssetBuildStats last_build_;
    std::string last_error_;
    schema::AssetCatalogDataT catalog_;
};

} // namespace engine
