#include "runtime/ui/ui_system.hpp"

#include "runtime/font/font_system.hpp"
#include "runtime/scene/node.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/ui_component.hpp"
#include "runtime/ui/rml_font.hpp"
#include "runtime/ui/rml_input.hpp"
#include "runtime/ui/rml_render.hpp"
#include "runtime/window/window_system.hpp"
#include "runtime/world/world_system.hpp"
#include <RmlUi/Core.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <deque>

namespace engine
{
namespace
{
class RmlPlatform final : public Rml::SystemInterface
{
public:
    double GetElapsedTime() override
    {
        return static_cast<double>(SDL_GetTicksNS()) / 1.0e9;
    }
    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override
    {
        SDL_LogMessage(SDL_LOG_CATEGORY_APPLICATION,
                       type <= Rml::Log::LT_WARNING ? SDL_LOG_PRIORITY_WARN : SDL_LOG_PRIORITY_INFO,
                       "RmlUi: %s", message.c_str());
        return true;
    }
    void SetClipboardText(const Rml::String& text) override
    {
        static_cast<void>(SDL_SetClipboardText(text.c_str()));
    }
    void GetClipboardText(Rml::String& text) override
    {
        char* value = SDL_GetClipboardText();
        text = value ? value : "";
        SDL_free(value);
    }
    void JoinPath(Rml::String& result, const Rml::String&, const Rml::String& path) override
    {
        // UI 资源使用 catalog 的完整虚拟路径，不能变成宿主文件路径。
        result = path;
    }
};
// 外部 CSS、脚本及字体源文件不参与运行时加载；资源均由明确依赖交付。
class RmlFiles final : public Rml::FileInterface
{
public:
    Rml::FileHandle Open(const Rml::String&) override
    {
        return 0;
    }
    void Close(Rml::FileHandle) override
    {
    }
    size_t Read(void*, size_t, Rml::FileHandle) override
    {
        return 0;
    }
    bool Seek(Rml::FileHandle, long, int) override
    {
        return false;
    }
    size_t Tell(Rml::FileHandle) override
    {
        return 0;
    }
};
} // namespace
struct UiSystem::State final : Rml::EventListener
{
    struct Document
    {
        std::uint64_t node = 0;
        ResourcePtr resource;
        Rml::Context* context = nullptr;
    };
    explicit State(FontSystem& fonts) : renderer(fonts), font_engine(fonts)
    {
    }
    RmlPlatform platform;
    RmlFiles files;
    ui_detail::RmlRender renderer;
    ui_detail::RmlFont font_engine;
    std::vector<Document> documents;
    std::deque<UiEvent> events;
    std::uint64_t scene_id = 0;
    bool initialized = false;
    Rml::Context* keyboard_context = nullptr;
    Rml::Context* pointer_context = nullptr;
    std::uint32_t pointer_buttons = 0;
    void ProcessEvent(Rml::Event& event) override
    {
        auto* target = event.GetTargetElement();
        if (!target)
        {
            return;
        }
        while (target->GetId().empty() && target->GetParentNode())
        {
            target = target->GetParentNode();
        }
        if (target->GetId().empty())
        {
            return;
        }
        for (const auto& document : documents)
        {
            if (document.context == target->GetContext())
            {
                if (events.size() == 256)
                {
                    events.pop_front();
                }
                events.push_back({document.node, target->GetId(), event.GetType()});
                break;
            }
        }
    }
};
UiSystem::UiSystem(WindowSystem& window, WorldSystem& world, FontSystem& fonts)
    : state_(std::make_unique<State>(fonts)), window_(window), world_(world)
{
}
UiSystem::~UiSystem() noexcept
{
    shutdown();
}
bool UiSystem::on_initialize(const EngineConfig&)
{
    Rml::SetSystemInterface(&state_->platform);
    Rml::SetFileInterface(&state_->files);
    Rml::SetRenderInterface(&state_->renderer);
    Rml::SetFontEngineInterface(&state_->font_engine);
    state_->initialized = Rml::Initialise();
    return state_->initialized;
}
bool UiSystem::render(std::uint32_t width, std::uint32_t height)
{
    if (!state_->initialized)
    {
        return false;
    }
    const auto* scene = world_.scene();
    if (!scene)
    {
        release_scene();
        return true;
    }
    std::vector<State::Document> wanted;
    for (const auto& node : scene->nodes())
    {
        const auto* ui = node->get_component<UiComponent>();
        if (ui && ui->enabled() && ui->document())
        {
            wanted.push_back({node->id(), ui->document(), nullptr});
        }
    }
    const bool changed = state_->scene_id != scene->id() ||
                         wanted.size() != state_->documents.size() ||
                         !std::equal(wanted.begin(), wanted.end(), state_->documents.begin(),
                                     [](const auto& a, const auto& b)
                                     { return a.node == b.node && a.resource == b.resource; });
    int logical_width = 0, logical_height = 0;
    if (!SDL_GetWindowSize(window_.native_window(), &logical_width, &logical_height))
    {
        return false;
    }
    if (changed)
    {
        release_scene();
        state_->scene_id = scene->id();
        for (auto& wanted_document : wanted)
        {
            const auto& data = *wanted_document.resource->ui();
            if (!state_->font_engine.activate(wanted_document.resource))
            {
                release_scene();
                return false;
            }
            const auto name = "scene-ui-" + std::to_string(wanted_document.node);
            wanted_document.context = Rml::CreateContext(name, {logical_width, logical_height});
            if (!wanted_document.context)
            {
                release_scene();
                return false;
            }
            state_->documents.push_back(std::move(wanted_document));
            auto* context = state_->documents.back().context;
            state_->renderer.set_document(data);
            auto* document = context->LoadDocumentFromMemory(
                data.markup, state_->documents.back().resource->info().path);
            if (!document)
            {
                release_scene();
                return false;
            }
            document->AddEventListener("click", state_.get());
            document->AddEventListener("change", state_.get());
            document->Show();
        }
    }
    if (state_->documents.empty())
    {
        return true;
    }
    if (!state_->renderer.begin(width, height, logical_width, logical_height))
    {
        return false;
    }
    for (auto& document : state_->documents)
    {
        document.context->SetDimensions({logical_width, logical_height});
        if (!state_->font_engine.activate(document.resource))
        {
            return false;
        }
        state_->renderer.set_document(*document.resource->ui());
        if (!document.context->Update() || !document.context->Render())
        {
            return false;
        }
    }
    return state_->renderer.healthy();
}
void UiSystem::release_scene() noexcept
{
    if (!state_ || !state_->initialized)
    {
        return;
    }
    for (auto& document : state_->documents)
    {
        static_cast<void>(Rml::RemoveContext(document.context->GetName()));
    }
    state_->keyboard_context = nullptr;
    state_->pointer_context = nullptr;
    state_->pointer_buttons = 0;
    state_->documents.clear();
    state_->events.clear();
    state_->scene_id = 0;
    Rml::ReleaseRenderManagers();
    Rml::ReleaseFontResources();
    state_->renderer.release();
    if (SDL_TextInputActive(window_.native_window()))
    {
        static_cast<void>(SDL_StopTextInput(window_.native_window()));
    }
}
bool UiSystem::process_event(const SDL_Event& event)
{
    if (!state_->initialized || state_->documents.empty() ||
        SDL_GetWindowFromEvent(&event) != window_.native_window())
    {
        return false;
    }
    const auto process = [&](State::Document& document)
    {
        if (!state_->font_engine.activate(document.resource))
        {
            return false;
        }
        state_->renderer.set_document(*document.resource->ui());
        return ui_detail::process_input(*document.context, window_.native_window(), event);
    };
    const auto select_focus = [&](Rml::Context* selected)
    {
        for (auto& document : state_->documents)
        {
            if (document.context != selected)
            {
                if (auto* focused = document.context->GetFocusElement())
                {
                    focused->Blur();
                }
            }
        }
        state_->keyboard_context = selected;
    };
    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
    {
        for (auto& document : state_->documents)
        {
            static_cast<void>(process(document));
        }
        select_focus(nullptr);
        state_->pointer_context = nullptr;
        state_->pointer_buttons = 0;
        return false;
    }
    if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP ||
        event.type == SDL_EVENT_TEXT_INPUT)
    {
        if (!state_->keyboard_context)
        {
            state_->keyboard_context = state_->documents.back().context;
        }
        for (auto& document : state_->documents)
        {
            if (document.context == state_->keyboard_context)
            {
                return process(document);
            }
        }
        return false;
    }
    const auto button_mask = [&]() -> std::uint32_t
    {
        return event.button.button > 0 && event.button.button <= 32
                   ? (std::uint32_t{1} << (event.button.button - 1))
                   : 0;
    };
    if (state_->pointer_context &&
        (event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
         event.type == SDL_EVENT_MOUSE_BUTTON_UP))
    {
        for (auto& document : state_->documents)
        {
            if (document.context == state_->pointer_context)
            {
                const bool consumed = process(document);
                if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
                {
                    state_->pointer_buttons |= button_mask();
                }
                else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP)
                {
                    state_->pointer_buttons &= ~button_mask();
                    if (state_->pointer_buttons == 0)
                    {
                        state_->pointer_context = nullptr;
                    }
                }
                return consumed;
            }
        }
    }
    for (auto it = state_->documents.rbegin(); it != state_->documents.rend(); ++it)
    {
        if (process(*it))
        {
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
            {
                select_focus(it->context);
                state_->pointer_context = it->context;
                state_->pointer_buttons = button_mask();
            }
            return true;
        }
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
    {
        select_focus(nullptr);
    }
    return false;
}
std::size_t UiSystem::document_count() const noexcept
{
    return state_->documents.size();
}
bool UiSystem::poll_event(UiEvent& output)
{
    output = {};
    if (state_->events.empty())
    {
        return false;
    }
    output = std::move(state_->events.front());
    state_->events.pop_front();
    return true;
}
void UiSystem::on_shutdown() noexcept
{
    if (state_->initialized)
    {
        release_scene();
        Rml::Shutdown();
        state_->initialized = false;
    }
    Rml::SetFontEngineInterface(nullptr);
    Rml::SetRenderInterface(nullptr);
    Rml::SetFileInterface(nullptr);
    Rml::SetSystemInterface(nullptr);
}
} // namespace engine
