#include "runtime/resource/resource_read.hpp"

#include "runtime/resource/resource_read_state.hpp"

#include <utility>

namespace engine
{

ResourceRead::ResourceRead(std::shared_ptr<State> state) noexcept : state_(std::move(state))
{
}

ResourceRead::~ResourceRead() noexcept
{
    (void)cancel();
}

ResourceReadStatus ResourceRead::status() const noexcept
{
    return state_->status;
}

bool ResourceRead::take(std::unique_ptr<Resource>& output)
{
    if (output || state_->status != ResourceReadStatus::ready)
    {
        return false;
    }
    output = std::move(state_->resource);
    state_->status = ResourceReadStatus::delivered;
    return true;
}

bool ResourceRead::cancel() noexcept
{
    return state_->cancel();
}

ResourceError ResourceRead::error() const noexcept
{
    return state_->error;
}

std::string_view ResourceRead::last_error() const noexcept
{
    if (state_->error == ResourceError::cancelled)
    {
        return "Resource read cancelled";
    }
    return state_->message;
}

} // namespace engine
