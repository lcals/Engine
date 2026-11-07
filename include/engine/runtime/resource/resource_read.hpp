#pragma once

#include "runtime/resource/resource_handle.hpp"

#include <memory>
#include <string_view>

namespace engine
{

class Resource;
class ResourceSystem;

enum class ResourceReadStatus : std::uint8_t
{
    reading,
    ready,
    delivered,
    failed,
    cancelled
};

// 单项读取，不表示依赖闭包就绪；只有 ResourceSystem 推进，查询不执行工作。
class ResourceRead final
{
public:
    ~ResourceRead() noexcept;
    ResourceRead(const ResourceRead&) = delete;
    ResourceRead& operator=(const ResourceRead&) = delete;
    ResourceRead(ResourceRead&&) = delete;
    ResourceRead& operator=(ResourceRead&&) = delete;

    [[nodiscard]] ResourceReadStatus status() const noexcept;
    [[nodiscard]] bool take(std::unique_ptr<Resource>& output);
    [[nodiscard]] bool cancel() noexcept;
    [[nodiscard]] ResourceError error() const noexcept;
    [[nodiscard]] std::string_view last_error() const noexcept;

private:
    friend class ResourceSystem;
    struct State;
    explicit ResourceRead(std::shared_ptr<State> state) noexcept;
    std::shared_ptr<State> state_;
};

} // namespace engine
