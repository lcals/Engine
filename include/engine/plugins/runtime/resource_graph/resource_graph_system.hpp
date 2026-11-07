#pragma once

#include "runtime/resource/resource_loader.hpp"
#include "runtime/system/system.hpp"

#include <memory>

namespace engine
{
class ResourceSystem;
class TaskSystem;
} // namespace engine

namespace engine::plugins::resource_graph
{
struct ResourceGraphConfig
{
    std::uint32_t max_requests = 128;
    std::uint32_t max_pending_resources = 1024;
    std::uint32_t completion_budget = 16;
};

class ResourceGraphSystem final : public System, public ResourceLoader
{
public:
    // 两个服务先初始化、后关闭；tasks 是资源组织专用域。
    ResourceGraphSystem(ResourceSystem& resources, TaskSystem& tasks,
                        const ResourceGraphConfig& config);
    ~ResourceGraphSystem() noexcept override;

    using ResourceLoader::get;
    [[nodiscard]] bool load(std::string_view path_or_guid, ResourceHandle& output) override;
    [[nodiscard]] bool begin_load(std::string_view path_or_guid,
                                  std::unique_ptr<ResourceLoad>& output) override;
    [[nodiscard]] bool load_memory(std::unique_ptr<Resource>& candidate,
                                   std::span<const ResourceHandle> dependencies,
                                   ResourceHandle& output) override;
    [[nodiscard]] bool begin_load_memory(std::unique_ptr<Resource>& candidate,
                                         std::span<const ResourceHandle> dependencies,
                                         std::unique_ptr<ResourceLoad>& output) override;
    [[nodiscard]] bool acquire_loaded(std::string_view path_or_guid,
                                      ResourceHandle& output) override;
    [[nodiscard]] bool retain(const ResourceHandle& handle, ResourceHandle& output) override;
    [[nodiscard]] bool retain_dependency(const ResourceHandle& owner, std::string_view guid,
                                         ResourceHandle& output) override;
    [[nodiscard]] bool release(ResourceHandle& handle) noexcept override;
    [[nodiscard]] ResourceError get(const ResourceHandle& handle,
                                    const Resource*& output) const noexcept override;
    [[nodiscard]] ResourceError error() const noexcept override;
    [[nodiscard]] std::string_view last_error() const noexcept override;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    [[nodiscard]] bool on_tick(double delta_time) override;
    void on_shutdown() noexcept override;

private:
    friend class GraphResourceLoad;
    struct State;
    std::shared_ptr<State> state_;
};
} // namespace engine::plugins::resource_graph
