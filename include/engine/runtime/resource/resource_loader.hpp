#pragma once

#include "runtime/resource/resource.hpp"
#include "runtime/resource/resource_load.hpp"

#include <memory>
#include <span>
#include <string_view>

namespace engine
{
// 只定义高层契约。加载、共享、计数和卸载由宿主注入的实现负责。
class ResourceLoader
{
public:
    virtual ~ResourceLoader() noexcept = default;
    ResourceLoader(const ResourceLoader&) = delete;
    ResourceLoader& operator=(const ResourceLoader&) = delete;
    ResourceLoader(ResourceLoader&&) = delete;
    ResourceLoader& operator=(ResourceLoader&&) = delete;

    [[nodiscard]] virtual bool load(std::string_view path_or_guid, ResourceHandle& output) = 0;
    [[nodiscard]] virtual bool begin_load(std::string_view path_or_guid,
                                          std::unique_ptr<ResourceLoad>& output) = 0;
    [[nodiscard]] virtual bool load_memory(std::unique_ptr<Resource>& candidate,
                                           std::span<const ResourceHandle> dependencies,
                                           ResourceHandle& output) = 0;
    [[nodiscard]] virtual bool begin_load_memory(std::unique_ptr<Resource>& candidate,
                                                 std::span<const ResourceHandle> dependencies,
                                                 std::unique_ptr<ResourceLoad>& output) = 0;
    [[nodiscard]] virtual bool acquire_loaded(std::string_view path_or_guid,
                                              ResourceHandle& output) = 0;
    [[nodiscard]] virtual bool retain(const ResourceHandle& handle, ResourceHandle& output) = 0;
    [[nodiscard]] virtual bool retain_dependency(const ResourceHandle& owner,
                                                 std::string_view path_or_guid,
                                                 ResourceHandle& output) = 0;
    [[nodiscard]] virtual bool release(ResourceHandle& handle) noexcept = 0;
    [[nodiscard]] virtual ResourceError get(const ResourceHandle& handle,
                                            const Resource*& output) const noexcept = 0;
    template <typename TResource>
    [[nodiscard]] ResourceError get(const ResourceHandle& handle,
                                    const TResource*& output) const noexcept
    {
        output = nullptr;
        const Resource* resource = nullptr;
        const auto status = get(handle, resource);
        if (status != ResourceError::none)
        {
            return status;
        }
        output = resource_detail::cast<TResource>(resource);
        return output ? ResourceError::none : ResourceError::type_mismatch;
    }
    [[nodiscard]] virtual ResourceError error() const noexcept = 0;
    [[nodiscard]] virtual std::string_view last_error() const noexcept = 0;

protected:
    ResourceLoader() noexcept;
    [[nodiscard]] std::uint64_t owner_id() const noexcept;

private:
    const std::uint64_t owner_id_;
};
} // namespace engine
