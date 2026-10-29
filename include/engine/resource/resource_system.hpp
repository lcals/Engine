#pragma once

#include "system.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace engine
{

class ResourceSystem final : public System
{
public:
    ResourceSystem() noexcept;
    ~ResourceSystem() noexcept override;

    // 同步读取普通文件，不要求系统初始化；上限 64 MiB，失败清空输出。
    [[nodiscard]] static bool read_binary(const std::filesystem::path& path,
                                          std::vector<std::byte>& output);
    // 保留原始字节及 NUL，不转码；空文件成功。
    [[nodiscard]] static bool read_text(const std::filesystem::path& path, std::string& output);

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    void on_shutdown() noexcept override;
};

} // namespace engine
