#include "runtime/resource/resource.hpp"

namespace engine
{
Resource::Resource(schema::AssetEntryT info) : info_(std::move(info))
{
}
Resource::~Resource() noexcept = default;
const schema::AssetEntryT& Resource::info() const noexcept
{
    return info_;
}
std::uint64_t Resource::generation() const noexcept
{
    return generation_;
}
} // namespace engine
