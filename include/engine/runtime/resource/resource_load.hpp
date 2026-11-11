#pragma once

#include "runtime/resource/resource_handle.hpp"

#include <cstdint>
#include <string_view>

namespace engine
{
enum class ResourceLoadStatus
{
    loading,
    ready,
    delivered,
    failed,
    cancelled
};

struct ResourceLoadProgress
{
    std::uint32_t completed = 0;
    std::uint32_t total = 0;
    bool discovery_complete = false;
    std::uint32_t in_flight = 0;
    std::uint64_t progress_revision = 0;
    std::uint64_t last_progress_ns = 0;
};

class ResourceLoad
{
public:
    virtual ~ResourceLoad() noexcept = default;
    ResourceLoad(const ResourceLoad&) = delete;
    ResourceLoad& operator=(const ResourceLoad&) = delete;
    ResourceLoad(ResourceLoad&&) = delete;
    ResourceLoad& operator=(ResourceLoad&&) = delete;

    [[nodiscard]] virtual ResourceLoadStatus
    status(ResourceLoadProgress& output) const noexcept = 0;
    [[nodiscard]] virtual bool take(ResourceHandle& output) = 0;
    [[nodiscard]] virtual bool cancel() noexcept = 0;
    [[nodiscard]] virtual ResourceError error() const noexcept = 0;
    [[nodiscard]] virtual std::string_view last_error() const noexcept = 0;

protected:
    ResourceLoad() noexcept = default;
};
} // namespace engine
