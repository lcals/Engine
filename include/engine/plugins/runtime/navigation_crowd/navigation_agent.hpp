#pragma once

#include "runtime/navigation/navigation_agent.hpp"
#include "runtime/scene/component.hpp"

#include <cstdint>

namespace engine::plugins::navigation_crowd
{

class NavigationAgent final : public Component
{
public:
    static constexpr ComponentType component_type = ComponentType::NavigationAgentData;
    [[nodiscard]] static bool valid_settings(const schema::NavigationAgentDataT& value) noexcept;
    [[nodiscard]] const schema::NavigationAgentDataT& settings() const noexcept;
    [[nodiscard]] bool set_settings(const schema::NavigationAgentDataT& value) noexcept;
    // 接受请求不代表可达；由下次原生导航更新发布状态。
    [[nodiscard]] bool move_to(const Vec3& target) noexcept;
    void stop() noexcept;
    [[nodiscard]] NavigationAgentStatus state() const noexcept;
    [[nodiscard]] Vec3 velocity() const noexcept;
    [[nodiscard]] std::uint64_t instance_id() const noexcept;
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& value) override;

private:
    friend class engine::Node;
    friend class NavigationCrowdSystem;
    explicit NavigationAgent(Node& node);
    [[nodiscard]] bool initialize() override;

    const std::uint64_t instance_id_;
    std::uint64_t settings_revision_ = 1;
    std::uint64_t request_revision_ = 1;
    Vec3 target_{};
    Vec3 velocity_{};
    bool has_target_ = false;
    NavigationAgentStatus state_ = NavigationAgentStatus::idle;
};

} // namespace engine::plugins::navigation_crowd
