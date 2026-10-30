#pragma once

#include "runtime/system/system.hpp"
#include "runtime/ui/ui_event.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

union SDL_Event;
namespace engine
{
class WindowSystem;
class WorldSystem;
class FontSystem;
class UiSystem final : public System
{
public:
    // 借用对象须存活到关闭；绘制必须在渲染设备就绪到关闭之间调用。
    UiSystem(WindowSystem& window, WorldSystem& world, FontSystem& fonts);
    ~UiSystem() noexcept override;
    [[nodiscard]] bool render(std::uint32_t width, std::uint32_t height);
    // Prepare documents without drawing. View operations synchronize before resolving handles.
    [[nodiscard]] bool synchronize();
    [[nodiscard]] bool find_element(std::uint64_t node, std::string_view id,
                                    UiElementHandle& output);
    [[nodiscard]] bool is_valid(UiElementHandle handle);
    [[nodiscard]] std::uint64_t document_id(std::uint64_t node);
    [[nodiscard]] bool document_visible(std::uint64_t node);
    [[nodiscard]] bool set_document_visible(std::uint64_t node, bool visible);
    // Failed reads clear output; failed writes do not change the target.
    [[nodiscard]] bool element_id(UiElementHandle handle, std::string& output);
    [[nodiscard]] bool text(UiElementHandle handle, std::string& output);
    [[nodiscard]] bool value(UiElementHandle handle, std::string& output);
    [[nodiscard]] bool set_text(UiElementHandle handle, std::string_view value);
    [[nodiscard]] bool set_value(UiElementHandle handle, std::string_view value);
    [[nodiscard]] bool set_visible(UiElementHandle handle, bool value);
    [[nodiscard]] bool set_enabled(UiElementHandle handle, bool value);
    [[nodiscard]] bool set_class(UiElementHandle handle, std::string_view name, bool active);
    [[nodiscard]] bool set_style(UiElementHandle handle, std::string_view name,
                                 std::string_view value);
    [[nodiscard]] bool set_image(UiElementHandle handle, std::string_view path);
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
