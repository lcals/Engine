#pragma once

#include "runtime/system/system.hpp"

#include <cstdint>
#include <memory>
#include <string_view>

struct ecs_world_t;

namespace engine
{

class Scene;

class WorldSystem final : public System
{
public:
    WorldSystem() noexcept;
    ~WorldSystem() noexcept override;

    // 返回借用；已有场景或世界未初始化时失败。
    [[nodiscard]] Scene* create_scene(std::string_view name);
    void destroy_scene() noexcept;
    [[nodiscard]] Scene* scene() noexcept;
    [[nodiscard]] const Scene* scene() const noexcept;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    [[nodiscard]] bool on_tick(double delta_time) override;
    void on_shutdown() noexcept override;

private:
    ecs_world_t* world_ = nullptr;
    std::unique_ptr<Scene> scene_;
    std::uint64_t scene_sequence_ = 0;
};

} // namespace engine
