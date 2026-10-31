#pragma once

#include "runtime/render/render_extension.hpp"
#include "runtime/system/system.hpp"

#include <memory>
#include <string>

namespace engine
{
class WorldSystem;
class RenderSystem;
} // namespace engine

namespace engine::plugins::effects
{

class EffectSystem final : public System, public RenderExtension
{
public:
    // world 覆盖本系统生命期；renderer 只借用到成功初始化，之后由基类管理登记。
    explicit EffectSystem(WorldSystem& world, RenderSystem& renderer) noexcept;
    ~EffectSystem() noexcept override;

    [[nodiscard]] bool draw(const RenderContext& context) override;
    void release_scene() noexcept override;
    void release_gpu() noexcept override;
    [[nodiscard]] const std::string& last_error() const noexcept;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    [[nodiscard]] bool on_tick(double delta_time) override;
    void on_shutdown() noexcept override;

private:
    struct State;
    WorldSystem& world_;
    RenderSystem* registration_target_ = nullptr;
    std::unique_ptr<State> state_;
    std::string last_error_;
    double pending_seconds_ = 0.0;
};

} // namespace engine::plugins::effects
