#pragma once

#include "runtime/resource/resource_loader.hpp"

#include <utility>

namespace engine
{
// 独占一次资源获取。只能显式 retain，禁止复制；清理在协调线程执行。
// 加载器借用必须覆盖此对象及其保活的数据使用。
class ResourceReference final
{
public:
    ResourceReference() noexcept = default;
    ~ResourceReference() noexcept
    {
        reset();
    }
    ResourceReference(const ResourceReference&) = delete;
    ResourceReference& operator=(const ResourceReference&) = delete;
    ResourceReference(ResourceReference&& other) noexcept
        : loader_(std::exchange(other.loader_, nullptr)), handle_(std::exchange(other.handle_, {}))
    {
    }
    ResourceReference& operator=(ResourceReference&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            loader_ = std::exchange(other.loader_, nullptr);
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }
    [[nodiscard]] bool acquire(ResourceLoader* loader, std::string_view path_or_guid)
    {
        if (loader_ || !loader || !loader->acquire_loaded(path_or_guid, handle_))
        {
            return false;
        }
        loader_ = loader;
        return true;
    }
    [[nodiscard]] bool retain(ResourceLoader* loader, const ResourceHandle& source)
    {
        if (loader_ || !loader || !loader->retain(source, handle_))
        {
            return false;
        }
        loader_ = loader;
        return true;
    }
    [[nodiscard]] bool retain(const ResourceReference& source)
    {
        return retain(source.loader_, source.handle_);
    }
    [[nodiscard]] bool dependency(ResourceLoader* loader, const ResourceHandle& owner,
                                  std::string_view path_or_guid)
    {
        if (loader_ || !loader || !loader->retain_dependency(owner, path_or_guid, handle_))
        {
            return false;
        }
        loader_ = loader;
        return true;
    }
    [[nodiscard]] bool adopt(ResourceLoader* loader, ResourceHandle& handle)
    {
        const Resource* resource = nullptr;
        if (loader_ || !loader || loader->get(handle, resource) != ResourceError::none)
        {
            return false;
        }
        loader_ = loader;
        handle_ = std::exchange(handle, {});
        return true;
    }
    void reset() noexcept
    {
        if (loader_)
        {
            (void)loader_->release(handle_);
        }
        handle_ = {};
        loader_ = nullptr;
    }
    [[nodiscard]] ResourceHandle detach() noexcept
    {
        loader_ = nullptr;
        return std::exchange(handle_, {});
    }
    [[nodiscard]] ResourceHandle handle() const noexcept
    {
        return handle_;
    }
    [[nodiscard]] ResourceLoader* loader() const noexcept
    {
        return loader_;
    }
    [[nodiscard]] const Resource* get() const noexcept
    {
        const Resource* value = nullptr;
        if (loader_)
        {
            (void)loader_->get(handle_, value);
        }
        return value;
    }
    template <typename T> [[nodiscard]] const typename T::Data* data() const noexcept
    {
        const T* value = nullptr;
        return loader_ && loader_->get(handle_, value) == ResourceError::none ? &value->data()
                                                                              : nullptr;
    }
    explicit operator bool() const noexcept
    {
        return get() != nullptr;
    }
    [[nodiscard]] const Resource* operator->() const noexcept
    {
        return get();
    }
    [[nodiscard]] const Resource& operator*() const noexcept
    {
        return *get();
    }

private:
    ResourceLoader* loader_ = nullptr;
    ResourceHandle handle_;
};
} // namespace engine
