#pragma once
#include <cstdint>
union SDL_Event;
struct SDL_Window;
namespace Rml
{
class Context;
}
namespace engine::ui_detail
{
// 返回 true 表示 UI 消费输入，窗口生命周期事件仍由宿主处理。
[[nodiscard]] bool process_input(Rml::Context& context, SDL_Window* window, const SDL_Event& event,
                                 std::uint16_t modifiers);
// 清除 hover 后释放按键，避免安全取消被解释为 click。
void cancel_pointer(Rml::Context& context, std::uint32_t buttons);
} // namespace engine::ui_detail
