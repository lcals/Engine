#include "runtime/resource/resource_loader.hpp"

#include <atomic>
#include <limits>

namespace engine
{
namespace
{
std::uint64_t allocate_owner() noexcept
{
    static std::atomic<std::uint64_t> sequence{0};
    auto value = sequence.load(std::memory_order_relaxed);
    while (value != std::numeric_limits<std::uint64_t>::max())
    {
        if (sequence.compare_exchange_weak(value, value + 1, std::memory_order_relaxed))
        {
            return value + 1;
        }
    }
    return 0;
}
} // namespace

ResourceLoader::ResourceLoader() noexcept : owner_id_(allocate_owner())
{
}
std::uint64_t ResourceLoader::owner_id() const noexcept
{
    return owner_id_;
}
} // namespace engine
