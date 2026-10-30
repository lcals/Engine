#pragma once

#include "runtime/system/system.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

union SDL_Event;
namespace engine
{
class WindowSystem;
class WorldSystem;
class FontSystem;
struct UiEvent
{
    std::uint64_t node_id = 0;
    std::string element_id;
    std::string type;
};
class UiSystem final : public System
{
public:
    // 借用对象须存活到关闭；绘制必须在渲染设备就绪到关闭之间调用。
    UiSystem(WindowSystem& window, WorldSystem& world, FontSystem& fonts);
    ~UiSystem() noexcept override;
    [[nodiscard]] bool render(std::uint32_t width, std::uint32_t height);
    void release_scene() noexcept;
    [[nodiscard]] bool process_event(const SDL_Event& event);
    [[nodiscard]] std::size_t document_count() const noexcept;
    [[nodiscard]] bool poll_event(UiEvent& output);

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    void on_shutdown() noexcept override;

private:
    struct State;
    std::unique_ptr<State> state_;
    WindowSystem& window_;
    WorldSystem& world_;
};
} // namespace engine
