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
    default:
        return KI_UNKNOWN;
    }
}
int modifiers()
{
    const auto mask = SDL_GetModState();
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
bool process_input(Rml::Context& context, SDL_Window* window, const SDL_Event& event)
{
    if (SDL_GetWindowFromEvent(&event) != window)
    {
        return false;
    }
    const auto mods = modifiers();
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
} // namespace engine::ui_detail
