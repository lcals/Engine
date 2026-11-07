#pragma once

#include "runtime/resource/resource_loader.hpp"

#include <algorithm>
#include <vector>

namespace test_support
{
// Test decorator: preserve the injected implementation's handle identities and observe
// independent acquisitions. Observations never retain resources themselves.
class TrackedResourceLoader final : public engine::ResourceLoader
{
public:
    explicit TrackedResourceLoader(engine::ResourceLoader& loader) : loader_(loader)
    {
    }
    bool load(std::string_view path, engine::ResourceHandle& output) override
    {
        ++loads_;
        return observe(loader_.load(path, output), output);
    }
    bool begin_load(std::string_view path, std::unique_ptr<engine::ResourceLoad>& output) override
    {
        ++loads_;
        return loader_.begin_load(path, output);
    }
    bool load_memory(std::unique_ptr<engine::Resource>& candidate,
                     std::span<const engine::ResourceHandle> dependencies,
                     engine::ResourceHandle& output) override
    {
        return observe(loader_.load_memory(candidate, dependencies, output), output);
    }
    bool begin_load_memory(std::unique_ptr<engine::Resource>& candidate,
                           std::span<const engine::ResourceHandle> dependencies,
                           std::unique_ptr<engine::ResourceLoad>& output) override
    {
        return loader_.begin_load_memory(candidate, dependencies, output);
    }
    bool acquire_loaded(std::string_view path, engine::ResourceHandle& output) override
    {
        ++acquires_;
        return observe(loader_.acquire_loaded(path, output), output);
    }
    bool retain(const engine::ResourceHandle& handle, engine::ResourceHandle& output) override
    {
        return observe(loader_.retain(handle, output), output);
    }
    bool retain_dependency(const engine::ResourceHandle& owner, std::string_view guid,
                           engine::ResourceHandle& output) override
    {
        return observe(loader_.retain_dependency(owner, guid, output), output);
    }
    bool release(engine::ResourceHandle& handle) noexcept override
    {
        return loader_.release(handle);
    }
    engine::ResourceError get(const engine::ResourceHandle& handle,
                              const engine::Resource*& output) const noexcept override
    {
        return loader_.get(handle, output);
    }
    engine::ResourceError error() const noexcept override
    {
        return loader_.error();
    }
    std::string_view last_error() const noexcept override
    {
        return loader_.last_error();
    }
    std::size_t loads() const noexcept
    {
        return loads_;
    }
    std::size_t acquires() const noexcept
    {
        return acquires_;
    }
    bool released() const
    {
        return std::all_of(handles_.begin(), handles_.end(),
                           [&](const auto& handle)
                           {
                               const engine::Resource* value = nullptr;
                               return loader_.get(handle, value) != engine::ResourceError::none;
                           });
    }
    std::size_t live(std::string_view guid, std::uint64_t generation) const
    {
        return std::count_if(handles_.begin(), handles_.end(),
                             [&](const auto& handle)
                             {
                                 const engine::Resource* value = nullptr;
                                 return loader_.get(handle, value) == engine::ResourceError::none &&
                                        value->info().guid == guid &&
                                        value->generation() == generation;
                             });
    }

private:
    bool observe(bool success, const engine::ResourceHandle& handle)
    {
        if (success)
        {
            handles_.push_back(handle);
        }
        return success;
    }
    engine::ResourceLoader& loader_;
    std::vector<engine::ResourceHandle> handles_;
    std::size_t loads_ = 0;
    std::size_t acquires_ = 0;
};
} // namespace test_support
