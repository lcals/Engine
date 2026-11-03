#include "runtime/ui/rml_input.hpp"
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Input.h>
#include <SDL3/SDL.h>
namespace engine::ui_detail
{
namespace
{
Rml::Input::KeyIdentifier key(SDL_Keycode code)
{
    using namespace Rml::Input;
    if (code >= SDLK_A && code <= SDLK_Z)
    {
        return static_cast<KeyIdentifier>(KI_A + code - SDLK_A);
    }
    if (code >= SDLK_0 && code <= SDLK_9)
    {
        return static_cast<KeyIdentifier>(KI_0 + code - SDLK_0);
    }
    switch (code)
    {
    case SDLK_SPACE:
        return KI_SPACE;
    case SDLK_RETURN:
        return KI_RETURN;
    case SDLK_KP_ENTER:
        return KI_NUMPADENTER;
    case SDLK_ESCAPE:
        return KI_ESCAPE;
    case SDLK_TAB:
        return KI_TAB;
    case SDLK_BACKSPACE:
        return KI_BACK;
    case SDLK_DELETE:
        return KI_DELETE;
    case SDLK_LEFT:
        return KI_LEFT;
    case SDLK_RIGHT:
        return KI_RIGHT;
    case SDLK_UP:
        return KI_UP;
    case SDLK_DOWN:
        return KI_DOWN;
    case SDLK_HOME:
        return KI_HOME;
    case SDLK_END:
        return KI_END;
    case SDLK_PAGEUP:
        return KI_PRIOR;
    case SDLK_PAGEDOWN:
        return KI_NEXT;
    case SDLK_INSERT:
        return KI_INSERT;
    case SDLK_LSHIFT:
        return KI_LSHIFT;
    case SDLK_RSHIFT:
        return KI_RSHIFT;
    case SDLK_LCTRL:
        return KI_LCONTROL;
    case SDLK_RCTRL:
        return KI_RCONTROL;
    case SDLK_LALT:
        return KI_LMENU;
    case SDLK_RALT:
        return KI_RMENU;
    case SDLK_LGUI:
        return KI_LWIN;
    case SDLK_RGUI:
        return KI_RWIN;
    case SDLK_F1:
        return KI_F1;
    case SDLK_F2:
        return KI_F2;
    case SDLK_F3:
        return KI_F3;
    case SDLK_F4:
        return KI_F4;
    case SDLK_F5:
        return KI_F5;
    case SDLK_F6:
        return KI_F6;
    case SDLK_F7:
        return KI_F7;
    case SDLK_F8:
        return KI_F8;
    case SDLK_F9:
        return KI_F9;
    case SDLK_F10:
        return KI_F10;
    case SDLK_F11:
        return KI_F11;
    case SDLK_F12:
        return KI_F12;
    default:
        return KI_UNKNOWN;
    }
}
int modifiers(SDL_Keymod mask)
{
    int result = 0;
    if ((mask & SDL_KMOD_SHIFT) != 0)
    {
        result |= Rml::Input::KM_SHIFT;
    }
    if ((mask & SDL_KMOD_CTRL) != 0)
    {
        result |= Rml::Input::KM_CTRL;
    }
    if ((mask & SDL_KMOD_ALT) != 0)
    {
        result |= Rml::Input::KM_ALT;
    }
    if ((mask & SDL_KMOD_GUI) != 0)
    {
        result |= Rml::Input::KM_META;
    }
    if ((mask & SDL_KMOD_CAPS) != 0)
    {
        result |= Rml::Input::KM_CAPSLOCK;
    }
    return result;
}
int button(std::uint8_t value)
{
    if (value == SDL_BUTTON_LEFT)
    {
        return 0;
    }
    if (value == SDL_BUTTON_RIGHT)
    {
        return 1;
    }
    if (value == SDL_BUTTON_MIDDLE)
    {
        return 2;
    }
    return -1;
}
} // namespace
bool process_input(Rml::Context& context, SDL_Window* window, const SDL_Event& event,
                   std::uint16_t modifier_state)
{
    if (SDL_GetWindowFromEvent(&event) != window)
    {
        return false;
    }
    const auto mods = modifiers(modifier_state);
    bool consumed = false;
    switch (event.type)
    {
    case SDL_EVENT_MOUSE_MOTION:
        consumed = !context.ProcessMouseMove(static_cast<int>(event.motion.x),
                                             static_cast<int>(event.motion.y), mods);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (button(event.button.button) >= 0)
        {
            static_cast<void>(context.ProcessMouseMove(static_cast<int>(event.button.x),
                                                       static_cast<int>(event.button.y), mods));
            consumed = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN
                           ? !context.ProcessMouseButtonDown(button(event.button.button), mods)
                           : !context.ProcessMouseButtonUp(button(event.button.button), mods);
        }
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        consumed = !context.ProcessMouseWheel(Rml::Vector2f(-event.wheel.x, -event.wheel.y), mods);
        break;
    case SDL_EVENT_KEY_DOWN:
        consumed = !context.ProcessKeyDown(key(event.key.key), mods);
        break;
    case SDL_EVENT_KEY_UP:
        consumed = !context.ProcessKeyUp(key(event.key.key), mods);
        break;
    case SDL_EVENT_TEXT_INPUT:
        consumed = !context.ProcessTextInput(Rml::String(event.text.text));
        break;
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        static_cast<void>(context.ProcessMouseLeave());
        break;
    default:
        break;
    }
    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
    {
        if (SDL_TextInputActive(window))
        {
            static_cast<void>(SDL_StopTextInput(window));
        }
    }
    else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP ||
             event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP)
    {
        const auto* focused = context.GetFocusElement();
        const bool text =
            focused && (focused->GetTagName() == "input" || focused->GetTagName() == "textarea");
        if (text && !SDL_TextInputActive(window))
        {
            static_cast<void>(SDL_StartTextInput(window));
        }
        else if (!text && SDL_TextInputActive(window))
        {
            static_cast<void>(SDL_StopTextInput(window));
        }
    }
    return consumed;
}
void cancel_pointer(Rml::Context& context, std::uint32_t buttons)
{
    static_cast<void>(context.ProcessMouseLeave());
    for (const auto value : {SDL_BUTTON_LEFT, SDL_BUTTON_RIGHT, SDL_BUTTON_MIDDLE})
    {
        if ((buttons & SDL_BUTTON_MASK(value)) != 0)
        {
            static_cast<void>(context.ProcessMouseButtonUp(button(value), 0));
        }
    }
    static_cast<void>(context.ProcessMouseLeave());
}
} // namespace engine::ui_detail
