#pragma once

#include "runtime/animation/animation_events.hpp"
#include "runtime/animation/animation_retargeter.hpp"
#include "runtime/scene/scene_loading.hpp"
#include "runtime/system/system.hpp"

#include <cstddef>
#include <memory>

namespace engine
{

class WorldSystem;
class TaskSystem;

struct AnimationConfig
{
    std::size_t min_parallel_count = 8;
    std::size_t batch_size = 16;
};

class AnimationSystem final : public System
{
public:
    // 可空 world/tasks 由调用方拥有，存活期须覆盖本系统；tasks 须先初始化。
    explicit AnimationSystem(WorldSystem* world = nullptr, TaskSystem* tasks = nullptr,
                             const AnimationConfig& config = {},
                             std::unique_ptr<AnimationRetargeter> retargeter = nullptr) noexcept;
    ~AnimationSystem() noexcept override;
    [[nodiscard]] SceneWorkState prepare_scope(SceneScopeId scope, const SceneWorkBudget& budget);
    [[nodiscard]] bool scope_status(SceneScopeId scope, ScenePreparationSnapshot& output) const;
    void release_scope(SceneScopeId scope) noexcept;
    void clear() noexcept;
    void set_event_sink(AnimationEventSink sink);

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    [[nodiscard]] bool on_tick(double delta_time) override;
    void on_shutdown() noexcept override;

private:
    struct State;
    WorldSystem* world_ = nullptr;
    TaskSystem* tasks_ = nullptr;
    AnimationConfig config_;
    std::unique_ptr<State> state_;
    std::unique_ptr<AnimationRetargeter> retargeter_;
    AnimationEventSink event_sink_;
};

} // namespace engine
