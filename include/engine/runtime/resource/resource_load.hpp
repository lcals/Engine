#pragma once

#include "runtime/resource/resource.hpp"

#include <cstdint>
#include <string_view>

namespace engine
{

enum class ResourceLoadStatus
{
    loading,
    ready,
    failed,
};

struct ResourceLoadProgress
{
    std::uint32_t completed = 0;
    std::uint32_t total = 0;
};

// 调用方独占，同一线程显式推进；析构取消未完成操作并释放本任务引用。
class ResourceLoad
{
public:
    virtual ~ResourceLoad() noexcept = default;
    ResourceLoad(const ResourceLoad&) = delete;
    ResourceLoad& operator=(const ResourceLoad&) = delete;
    ResourceLoad(ResourceLoad&&) = delete;
    ResourceLoad& operator=(ResourceLoad&&) = delete;

    // 预算按完成的唯一资源项计数；0 只查询。单项读取/解码不承诺时间上限。
    [[nodiscard]] virtual ResourceLoadStatus step(std::uint32_t resource_budget,
                                                 ResourceLoadProgress& output) = 0;
    // 只有 ready 提供完整根引用；完成后的旧代快照可保留，但不能提交给新目录。
    [[nodiscard]] virtual ResourcePtr resource() const noexcept = 0;
    [[nodiscard]] virtual std::string_view last_error() const noexcept = 0;

protected:
    ResourceLoad() noexcept = default;
};

} // namespace engine
