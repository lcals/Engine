#pragma once

#include <filesystem>
#include <string>

namespace engine
{

struct AppConfig
{
    std::string application_name = "engine";
    std::filesystem::path builtin_assets_path;
    std::filesystem::path persistent_data_path;
    std::filesystem::path cache_path;
};

class App final
{
public:
    // 宿主串行初始化/关闭；路径不依赖编辑器项目。
    [[nodiscard]] static bool initialize(const AppConfig& config);
    static void shutdown() noexcept;
    [[nodiscard]] static bool is_initialized() noexcept;

    // 返回只读借用；初始化后稳定，shutdown 后内容清空。
    [[nodiscard]] static const std::filesystem::path& builtin_assets_path() noexcept;
    [[nodiscard]] static const std::filesystem::path& persistent_data_path() noexcept;
    [[nodiscard]] static const std::filesystem::path& cache_path() noexcept;

private:
    App() = delete;
};

} // namespace engine
