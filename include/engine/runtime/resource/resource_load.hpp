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

// 调用方独占，同一线程显式推进；后台读取/解码由实现调度，析构放弃结果且不等待。
class ResourceLoad
{
public:
    virtual ~ResourceLoad() noexcept = default;
    ResourceLoad(const ResourceLoad&) = delete;
    ResourceLoad& operator=(const ResourceLoad&) = delete;
    ResourceLoad(ResourceLoad&&) = delete;
    ResourceLoad& operator=(ResourceLoad&&) = delete;

    // 预算按完成的唯一资源项计数；0 只查询。异步模式只取就绪结果，不等待文件或任务。
    [[nodiscard]] virtual ResourceLoadStatus step(std::uint32_t resource_budget,
                                                  ResourceLoadProgress& output) = 0;
    // 只有 ready 提供完整根引用；完成后的旧代快照可保留，但不能提交给新目录。
    [[nodiscard]] virtual ResourcePtr resource() const noexcept = 0;
    [[nodiscard]] virtual std::string_view last_error() const noexcept = 0;

protected:
    ResourceLoad() noexcept = default;
};

} // namespace engine
