#pragma once

#include "runtime/system/system.hpp"

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>

namespace engine
{

class AssetPipelineSystem;

struct ResourceBuildStats
{
    std::size_t resource_count = 0;
    std::size_t package_count = 0;
    std::size_t reused_package_count = 0;
    std::filesystem::path output_path;
    std::string revision;
};

class AssetBuildSystem final : public System
{
public:
    explicit AssetBuildSystem(AssetPipelineSystem& pipeline) noexcept;
    ~AssetBuildSystem() noexcept override;

    AssetBuildSystem(const AssetBuildSystem&) = delete;
    AssetBuildSystem& operator=(const AssetBuildSystem&) = delete;
    AssetBuildSystem(AssetBuildSystem&&) = delete;
    AssetBuildSystem& operator=(AssetBuildSystem&&) = delete;

    // manifest 是项目 settings/ 下的相对路径；空 items 选择全部构建项。
    [[nodiscard]] bool build(const std::filesystem::path& project_root,
                             const std::filesystem::path& manifest,
                             std::span<const std::string> items = {});
    [[nodiscard]] const ResourceBuildStats& last_build() const noexcept;
    [[nodiscard]] const std::string& last_error() const noexcept;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    void on_shutdown() noexcept override;

private:
    AssetPipelineSystem& pipeline_; // 借用；先初始化，后关闭。
    ResourceBuildStats last_build_;
    std::string last_error_;
    bool building_ = false;
};

} // namespace engine
