#pragma once

#include "runtime/navigation/navigation_agent.hpp"
#include "runtime/navigation/navigation_bounds.hpp"
#include "runtime/navigation/navigation_geometry.hpp"
#include "runtime/navigation/navigation_settings.hpp"
#include "runtime/system/system.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace engine
{

class TaskSystem;

class NavigationSystem final : public System
{
public:
    // 可空借用已初始化任务域，须存活到导航关闭后；阈值范围 1..1024。
    // 达到阈值时独立 tile 并行准备，调用线程按序发布；不改变同步返回契约。
    explicit NavigationSystem(TaskSystem* tasks = nullptr,
                              std::size_t parallel_tile_threshold = 4) noexcept;
    ~NavigationSystem() noexcept override;

    // 主线程同步安全点；几何只借用到返回，不扫描场景，不与查询/修改并发。
    // 完整构建失败清空旧网格，避免误用过期几何。
    [[nodiscard]] bool build(std::span<const NavigationGeometry> geometry,
                             const NavigationBounds& bounds, const NavigationSettings& settings);
    // 提交全部当前几何；dirty 须覆盖旧位置及新位置，只重建交叠 tile（含必要边界）。
    // 失败移除受影响 tile 并递增 revision；非法 dirty 无法定位时清空全部。
    [[nodiscard]] bool replace(std::span<const NavigationGeometry> geometry,
                               const NavigationBounds& dirty);
    // 只返回完整可达路径；失败 output 为空，端点投影范围由 settings 指定。
    [[nodiscard]] bool find_path(const Vec3& start, const Vec3& end,
                                 std::vector<Vec3>& output) const;
    // 已构建网格上配置 1..4096 容量；同容量幂等，有代理时拒绝更改容量。
    [[nodiscard]] bool configure_crowd(std::uint32_t capacity);
    // 失败 output 为空句柄；尺寸不能超过烘焙净空，位置投影遵守 query_half_extents。
    [[nodiscard]] bool create_agent(const Vec3& position, const NavigationAgentSettings& settings,
                                    NavigationAgentHandle& output);
    [[nodiscard]] bool set_agent_settings(NavigationAgentHandle handle,
                                          const NavigationAgentSettings& settings);
    // 投影后排队，不等于可达；队列部分路径/失败报告 unreachable。
    [[nodiscard]] bool set_agent_target(NavigationAgentHandle handle, const Vec3& target);
    [[nodiscard]] bool stop_agent(NavigationAgentHandle handle);
    [[nodiscard]] bool remove_agent(NavigationAgentHandle handle);
    // 失败 output 为默认 invalid；所有状态为快照，不借出内部对象。
    [[nodiscard]] bool agent_state(NavigationAgentHandle handle,
                                   NavigationAgentState& output) const;
    // 由调用方显式推进，固定 1/60 秒，每次最多 8 步；不自动 tick。
    [[nodiscard]] bool step_crowd(double delta_time);
    // 完整 build / clear 使全部代理句柄失效；局部 replace 保留句柄并重建请求。
    void clear() noexcept;
    [[nodiscard]] bool ready() const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;
    [[nodiscard]] std::size_t rebuilt_tiles() const noexcept;
    [[nodiscard]] std::string_view last_error() const noexcept;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    void on_shutdown() noexcept override;

private:
    struct State;
    TaskSystem* tasks_ = nullptr;
    std::size_t parallel_tile_threshold_ = 4;
    std::unique_ptr<State> state_;
};

} // namespace engine
