#pragma once
union SDL_Event;
struct SDL_Window;
namespace Rml
{
class Context;
}
namespace engine::ui_detail
{
// 返回 true 表示 UI 消费输入，窗口生命周期事件仍由宿主处理。
[[nodiscard]] bool process_input(Rml::Context& context, SDL_Window* window, const SDL_Event& event);
} // namespace engine::ui_detail
