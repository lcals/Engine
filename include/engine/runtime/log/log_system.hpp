#pragma once

#include "runtime/system/system.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string_view>

namespace engine
{

enum class LogLevel : std::uint8_t
{
    info,
    warning,
    error
};

class LogSystem final : public System
{
public:
    [[nodiscard]] static LogSystem& instance() noexcept;
    static void write(LogLevel level, std::string_view message);
    ~LogSystem() noexcept override;

    LogSystem(const LogSystem&) = delete;
    LogSystem& operator=(const LogSystem&) = delete;
    LogSystem(LogSystem&&) = delete;
    LogSystem& operator=(LogSystem&&) = delete;

    [[nodiscard]] bool flush();
    // 宿主线程读取的只读借用；初始化后稳定，关闭后为空。
    [[nodiscard]] const std::filesystem::path& file_path() const noexcept;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    void on_shutdown() noexcept override;

private:
    LogSystem() noexcept;
    struct Output;

    std::mutex mutex_;
    std::shared_ptr<Output> output_;
    std::filesystem::path file_path_;
};

} // namespace engine
