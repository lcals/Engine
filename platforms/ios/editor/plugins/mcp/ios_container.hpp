#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace engine::plugins::mcp
{
struct McpTarget;

namespace ios_detail
{
class IosContainer final
{
public:
    // 配置只借用到本次工具调用结束；连接及锁由本对象独占。
    explicit IosContainer(const McpTarget& target) noexcept;
    ~IosContainer() noexcept;
    IosContainer(const IosContainer&) = delete;
    IosContainer& operator=(const IosContainer&) = delete;
    IosContainer(IosContainer&&) = delete;
    IosContainer& operator=(IosContainer&&) = delete;

    [[nodiscard]] bool open(std::string& error);
    [[nodiscard]] std::string_view runtime_dir() const noexcept;
    [[nodiscard]] bool lock(std::string& error);
    [[nodiscard]] bool directory(std::string_view path, bool& exists, std::string& error);
    [[nodiscard]] bool make_directory(std::string_view path, std::string& error);
    [[nodiscard]] bool read(std::string_view path, std::string& output, bool& exists,
                            std::string& error);
    [[nodiscard]] bool upload(const std::filesystem::path& source, std::string_view path,
                              std::string& error);
    [[nodiscard]] bool verify(std::string_view path, std::uint64_t size, std::string_view hash,
                              std::string& error);
    [[nodiscard]] bool rename(std::string_view source, std::string_view target, std::string& error);
    [[nodiscard]] bool remove(std::string_view path, std::string& error);

private:
    struct State;
    std::unique_ptr<State> state_;
    const McpTarget& target_;
};
} // namespace ios_detail
} // namespace engine::plugins::mcp
