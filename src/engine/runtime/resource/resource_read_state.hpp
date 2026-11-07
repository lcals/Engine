#pragma once

#include "runtime/resource/resource.hpp"
#include "runtime/resource/resource_read.hpp"

#include <atomic>
#include <string>

namespace engine
{

struct ResourceRead::State
{
    ResourceReadStatus status = ResourceReadStatus::reading;
    ResourceError error = ResourceError::none;
    std::string message;
    std::unique_ptr<Resource> resource;
    std::atomic<bool> cancelled{false};

    bool cancel() noexcept
    {
        if (status == ResourceReadStatus::delivered || status == ResourceReadStatus::failed)
        {
            return false;
        }
        cancelled.store(true, std::memory_order_relaxed);
        status = ResourceReadStatus::cancelled;
        error = ResourceError::cancelled;
        resource.reset();
        return true;
    }
};

} // namespace engine
