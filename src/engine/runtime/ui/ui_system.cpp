#include "runtime/ui/ui_system.hpp"

#include "runtime/font/font_system.hpp"
#include "runtime/resource/resource.hpp"
#include "runtime/scene/node.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/ui_component.hpp"
#include "runtime/ui/rml_font.hpp"
#include "runtime/ui/rml_input.hpp"
#include "runtime/ui/rml_render.hpp"
#include "runtime/window/window_system.hpp"
#include "runtime/world/world_system.hpp"
#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <atomic>
#include <deque>
#include <limits>
#include <map>
#include <optional>

namespace engine
{
namespace
{
bool valid_string(std::string_view value)
{
    return value.find('\0') == std::string_view::npos;
}

Rml::ElementFormControl* form_control(Rml::Element& element)
{
    // These built-in tags have fixed instancers; no native RTTI or custom element registration.
    const auto& tag = element.GetTagName();
    return tag == "input" || tag == "textarea" || tag == "select"
               ? static_cast<Rml::ElementFormControl*>(&element)
               : nullptr;
}

std::string element_value(Rml::Element& element)
{
    if (auto* control = form_control(element))
    {
        return control->GetValue();
    }
    return element.GetAttribute<Rml::String>("value", "");
}

bool element_enabled(Rml::Element& element)
{
    for (auto* current = &element; current != nullptr; current = current->GetParentNode())
    {
        if (current->HasAttribute("disabled"))
        {
            return false;
        }
    }
    return true;
}

void append_text(Rml::Element& element, std::string& output)
{
    if (element.GetTagName() == "#text")
    {
        output += static_cast<Rml::ElementText&>(element).GetText();
        return;
    }
    for (int index = 0; index < element.GetNumChildren(); ++index)
    {
        append_text(*element.GetChild(index), output);
    }
}

std::uint64_t next_document_id()
{
    static std::atomic<std::uint64_t> sequence{0};
    auto previous = sequence.load(std::memory_order_relaxed);
    while (previous != std::numeric_limits<std::uint64_t>::max())
    {
        if (sequence.compare_exchange_weak(previous, previous + 1, std::memory_order_relaxed))
        {
            return previous + 1;
        }
    }
    return 0;
}

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
    void JoinPath(Rml::String& result, const Rml::String& base, const Rml::String& path) override
    {
        // Keep project paths unchanged; RmlUi's cache sees a private document generation prefix.
        constexpr std::string_view prefix = "engine-ui/";
        const auto end = base.find('/', prefix.size());
        result = base.starts_with(prefix) && end != std::string::npos
                     ? base.substr(0, end + 1) + path
                     : path;
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
    struct Element
    {
        Rml::ObserverPtr<Rml::Element> reference;
        std::optional<Rml::Property> prior_focus;
        std::optional<Rml::Property> prior_pointer_events;
        bool disabled = false;
    };
    struct Document
    {
        std::uint64_t node = 0;
        std::uint64_t view = 0;
        std::uint64_t revision = 0;
        std::uint64_t id = 0;
        ResourcePtr resource;
        std::string key;
        Rml::Context* context = nullptr;
        Rml::ElementDocument* markup = nullptr;
        std::map<std::uint64_t, Element> elements;
        std::uint64_t element_sequence = 0;
        bool visible = true;
    };
    explicit State(FontSystem& fonts) : font_engine(fonts), renderer(fonts, font_engine)
    {
    }
    RmlPlatform platform;
    RmlFiles files;
    ui_detail::RmlFont font_engine;
    ui_detail::RmlRender renderer;
    std::vector<Document> documents;
    std::deque<UiEvent> events;
    std::uint64_t scene_id = 0;
    bool initialized = false;
    Rml::Context* keyboard_context = nullptr;
    Rml::Context* pointer_context = nullptr;
    std::uint32_t pointer_buttons = 0;
    bool input_event = false;
    Document* find_document(std::uint64_t node)
    {
        const auto found =
            std::find_if(documents.begin(), documents.end(),
                         [node](const auto& document) { return document.node == node; });
        return found == documents.end() ? nullptr : &*found;
    }
    bool activate(Document& document)
    {
        return font_engine.activate(document.key, document.resource) &&
               renderer.activate(document.key, document.resource);
    }
    void prune()
    {
        if (documents.empty())
        {
            // No remaining contexts can hold font faces or cached texture handles.
            Rml::ReleaseRenderManagers();
            font_engine.ReleaseFontResources();
            renderer.release();
            return;
        }
        std::vector<std::string> keys;
        keys.reserve(documents.size());
        for (const auto& document : documents)
        {
            keys.push_back(document.key);
        }
        renderer.prune(keys);
        font_engine.prune(keys);
    }
    UiElementHandle handle(Document& document, Rml::Element& element)
    {
        std::erase_if(document.elements, [](const auto& pair) { return !pair.second.reference; });
        for (const auto& [id, tracked] : document.elements)
        {
            if (tracked.reference.get() == &element)
            {
                return {document.id, id};
            }
        }
        if (document.element_sequence == std::numeric_limits<std::uint64_t>::max())
        {
            return {};
        }
        const auto id = ++document.element_sequence;
        Element tracked;
        tracked.reference = element.GetObserverPtr();
        document.elements.emplace(id, std::move(tracked));
        return {document.id, id};
    }
    Element* resolve(UiElementHandle handle, Document** owner = nullptr)
    {
        if (owner)
        {
            *owner = nullptr;
        }
        if (handle.document_id == 0 || handle.element_id == 0)
        {
            return nullptr;
        }
        for (auto& document : documents)
        {
            if (document.id != handle.document_id)
            {
                continue;
            }
            const auto found = document.elements.find(handle.element_id);
            if (found == document.elements.end() || !found->second.reference ||
                found->second.reference->GetOwnerDocument() != document.markup)
            {
                return nullptr;
            }
            if (owner)
            {
                *owner = &document;
            }
            return &found->second;
        }
        return nullptr;
    }
    void clear_events(std::uint64_t document_id)
    {
        std::erase_if(events, [document_id](const auto& event)
                      { return event.target.document_id == document_id; });
    }
    void close(Document& document)
    {
        if (keyboard_context == document.context)
        {
            keyboard_context = nullptr;
        }
        if (pointer_context == document.context)
        {
            pointer_context = nullptr;
            pointer_buttons = 0;
        }
        clear_events(document.id);
        document.elements.clear();
        static_cast<void>(Rml::RemoveContext(document.context->GetName()));
        document.context = nullptr;
        document.markup = nullptr;
    }
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
        if (target->GetId().empty() || !element_enabled(*target))
        {
            return;
        }
        for (auto& document : documents)
        {
            if (document.visible && document.context == target->GetContext())
            {
                const auto identity = handle(document, *target);
                if (identity.element_id == 0)
                {
                    return;
                }
                if (events.size() == 256)
                {
                    events.pop_front();
                }
                UiEvent output;
                output.node_id = document.node;
                output.element_id = target->GetId();
                output.type = event.GetType();
                output.scene_id = scene_id;
                output.target = identity;
                for (auto* current = target;
                     current && current->GetOwnerDocument() == document.markup;
                     current = current->GetParentNode())
                {
                    if (!current->GetId().empty())
                    {
                        const auto ancestor = handle(document, *current);
                        if (ancestor.element_id == 0)
                        {
                            return;
                        }
                        output.path.push_back(ancestor);
                    }
                }
                output.value = event.GetParameter<Rml::String>("value", element_value(*target));
                if (input_event && output.type == "change")
                {
                    auto input = output;
                    input.type = "input";
                    events.push_back(std::move(input));
                    if (events.size() == 256)
                    {
                        events.pop_front();
                    }
                }
                events.push_back(std::move(output));
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
bool UiSystem::synchronize()
{
    if (!state_->initialized)
    {
        return false;
    }
    const auto* scene = world_.scene();
    if (!scene)
    {
        if (state_->scene_id != 0 || !state_->documents.empty())
        {
            release_scene();
        }
        return true;
    }
    int logical_width = 0;
    int logical_height = 0;
    if (!SDL_GetWindowSize(window_.native_window(), &logical_width, &logical_height) ||
        logical_width <= 0 || logical_height <= 0)
    {
        return false;
    }
    if (state_->scene_id != scene->id())
    {
        release_scene();
        state_->scene_id = scene->id();
    }
    const auto wanted = [&](const State::Document& document)
    {
        const auto* node = scene->find_node(document.node);
        const auto* ui = node ? node->get_component<UiComponent>() : nullptr;
        return ui && ui->enabled() && ui->view_id() == document.view &&
               ui->document_revision() == document.revision && ui->document() == document.resource;
    };
    bool changed = false;
    std::erase_if(state_->documents,
                  [&](auto& document)
                  {
                      if (wanted(document))
                      {
                          return false;
                      }
                      state_->close(document);
                      changed = true;
                      return true;
                  });
    for (const auto& node : scene->nodes())
    {
        const auto* ui = node->get_component<UiComponent>();
        if (!ui || !ui->enabled() || !ui->document() || state_->find_document(node->id()))
        {
            continue;
        }
        State::Document candidate;
        candidate.node = node->id();
        candidate.view = ui->view_id();
        candidate.revision = ui->document_revision();
        candidate.id = next_document_id();
        candidate.resource = ui->document();
        candidate.key = candidate.resource->info().guid + "-" +
                        std::to_string(candidate.resource->generation());
        if (candidate.id == 0 || !state_->activate(candidate))
        {
            state_->prune();
            return false;
        }
        const auto name = "scene-ui-" + std::to_string(candidate.id);
        candidate.context = Rml::CreateContext(name, {logical_width, logical_height});
        if (!candidate.context)
        {
            state_->prune();
            return false;
        }
        candidate.markup = candidate.context->LoadDocumentFromMemory(
            candidate.resource->ui()->markup,
            "engine-ui/" + candidate.key + "/" + candidate.resource->info().path);
        if (!candidate.markup)
        {
            state_->close(candidate);
            state_->prune();
            return false;
        }
        candidate.markup->AddEventListener("click", state_.get());
        candidate.markup->AddEventListener("change", state_.get());
        candidate.markup->AddEventListener("input", state_.get());
        candidate.markup->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
        state_->documents.push_back(std::move(candidate));
        changed = true;
    }
    if (changed)
    {
        state_->prune();
    }
    const auto order = [&](std::uint64_t id)
    {
        return std::find_if(scene->nodes().begin(), scene->nodes().end(),
                            [id](const auto& node) { return node->id() == id; });
    };
    std::stable_sort(state_->documents.begin(), state_->documents.end(),
                     [&](const auto& left, const auto& right)
                     { return order(left.node) < order(right.node); });
    for (auto& document : state_->documents)
    {
        document.context->SetDimensions({logical_width, logical_height});
        if (!state_->activate(document) || !document.context->Update())
        {
            return false;
        }
    }
    if (!state_->keyboard_context && SDL_TextInputActive(window_.native_window()))
    {
        static_cast<void>(SDL_StopTextInput(window_.native_window()));
    }
    return true;
}
bool UiSystem::render(std::uint32_t width, std::uint32_t height)
{
    if (!synchronize())
    {
        return false;
    }
    if (state_->documents.empty())
    {
        return true;
    }
    int logical_width = 0;
    int logical_height = 0;
    if (!SDL_GetWindowSize(window_.native_window(), &logical_width, &logical_height) ||
        !state_->renderer.begin(width, height, logical_width, logical_height))
    {
        return false;
    }
    for (auto& document : state_->documents)
    {
        if (document.visible && (!state_->activate(document) || !document.context->Render()))
        {
            return false;
        }
    }
    return state_->renderer.healthy();
}
bool UiSystem::find_element(std::uint64_t node, std::string_view id, UiElementHandle& output)
{
    output = {};
    if (id.empty() || !valid_string(id) || !synchronize())
    {
        return false;
    }
    auto* document = state_->find_document(node);
    auto* element = document ? document->markup->GetElementById(std::string(id)) : nullptr;
    if (!element)
    {
        return false;
    }
    output = state_->handle(*document, *element);
    return output.element_id != 0;
}
bool UiSystem::is_valid(UiElementHandle handle)
{
    return synchronize() && state_->resolve(handle) != nullptr;
}
std::uint64_t UiSystem::document_id(std::uint64_t node)
{
    if (!synchronize())
    {
        return 0;
    }
    const auto* document = state_->find_document(node);
    return document ? document->id : 0;
}
bool UiSystem::document_visible(std::uint64_t node)
{
    if (!synchronize())
    {
        return false;
    }
    const auto* document = state_->find_document(node);
    return document && document->visible;
}
bool UiSystem::set_document_visible(std::uint64_t node, bool visible)
{
    if (!synchronize())
    {
        return false;
    }
    auto* document = state_->find_document(node);
    if (!document)
    {
        return false;
    }
    document->visible = visible;
    if (visible)
    {
        document->markup->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
    }
    else
    {
        document->markup->Hide();
        state_->clear_events(document->id);
        if (state_->keyboard_context == document->context)
        {
            state_->keyboard_context = nullptr;
            if (SDL_TextInputActive(window_.native_window()))
            {
                static_cast<void>(SDL_StopTextInput(window_.native_window()));
            }
        }
        if (state_->pointer_context == document->context)
        {
            state_->pointer_context = nullptr;
            state_->pointer_buttons = 0;
        }
    }
    return true;
}
bool UiSystem::element_id(UiElementHandle handle, std::string& output)
{
    output.clear();
    auto* element = synchronize() ? state_->resolve(handle) : nullptr;
    if (!element)
    {
        return false;
    }
    output = element->reference->GetId();
    return true;
}
bool UiSystem::text(UiElementHandle handle, std::string& output)
{
    output.clear();
    auto* element = synchronize() ? state_->resolve(handle) : nullptr;
    if (!element)
    {
        return false;
    }
    append_text(*element->reference.get(), output);
    return true;
}
bool UiSystem::value(UiElementHandle handle, std::string& output)
{
    output.clear();
    auto* element = synchronize() ? state_->resolve(handle) : nullptr;
    if (!element)
    {
        return false;
    }
    output = element_value(*element->reference.get());
    return true;
}
bool UiSystem::set_text(UiElementHandle handle, std::string_view value)
{
    if (!valid_string(value) || !synchronize())
    {
        return false;
    }
    State::Document* document = nullptr;
    auto* tracked = state_->resolve(handle, &document);
    if (!tracked || !state_->activate(*document))
    {
        return false;
    }
    auto& element = *tracked->reference.get();
    if (element.GetTagName() == "#text")
    {
        static_cast<Rml::ElementText&>(element).SetText(std::string(value));
        return true;
    }
    auto text_node = document->markup->CreateTextNode(std::string(value));
    if (!text_node)
    {
        return false;
    }
    while (element.GetNumChildren() > 0)
    {
        auto removed = element.RemoveChild(element.GetChild(0));
    }
    return element.AppendChild(std::move(text_node)) != nullptr;
}
bool UiSystem::set_value(UiElementHandle handle, std::string_view value)
{
    if (!valid_string(value) || !synchronize())
    {
        return false;
    }
    State::Document* document = nullptr;
    auto* tracked = state_->resolve(handle, &document);
    if (!tracked || !state_->activate(*document))
    {
        return false;
    }
    auto& element = *tracked->reference.get();
    if (auto* control = form_control(element))
    {
        control->SetValue(std::string(value));
    }
    else
    {
        element.SetAttribute("value", std::string(value));
    }
    return true;
}
bool UiSystem::set_visible(UiElementHandle handle, bool value)
{
    auto* tracked = synchronize() ? state_->resolve(handle) : nullptr;
    return tracked && tracked->reference->SetProperty("visibility", value ? "visible" : "hidden");
}
bool UiSystem::set_enabled(UiElementHandle handle, bool value)
{
    auto* tracked = synchronize() ? state_->resolve(handle) : nullptr;
    if (!tracked)
    {
        return false;
    }
    auto& element = *tracked->reference.get();
    if (auto* control = form_control(element))
    {
        control->SetDisabled(!value);
        return true;
    }
    if (!value)
    {
        if (!tracked->disabled)
        {
            if (const auto* property = element.GetLocalProperty(Rml::PropertyId::Focus))
            {
                tracked->prior_focus = *property;
            }
            if (const auto* property = element.GetLocalProperty(Rml::PropertyId::PointerEvents))
            {
                tracked->prior_pointer_events = *property;
            }
            if (!element.SetProperty("focus", "none") ||
                !element.SetProperty("pointer-events", "none"))
            {
                return false;
            }
            tracked->disabled = true;
        }
        element.SetAttribute("disabled", "");
    }
    else
    {
        element.RemoveAttribute("disabled");
        if (tracked->disabled)
        {
            if (tracked->prior_focus)
            {
                if (!element.SetProperty(Rml::PropertyId::Focus, *tracked->prior_focus))
                {
                    return false;
                }
            }
            else
            {
                element.RemoveProperty(Rml::PropertyId::Focus);
            }
            if (tracked->prior_pointer_events)
            {
                if (!element.SetProperty(Rml::PropertyId::PointerEvents,
                                         *tracked->prior_pointer_events))
                {
                    return false;
                }
            }
            else
            {
                element.RemoveProperty(Rml::PropertyId::PointerEvents);
            }
            tracked->prior_focus.reset();
            tracked->prior_pointer_events.reset();
            tracked->disabled = false;
        }
    }
    element.SetPseudoClass("disabled", !value);
    return true;
}
bool UiSystem::set_class(UiElementHandle handle, std::string_view name, bool active)
{
    if (name.empty() || !valid_string(name) ||
        std::any_of(name.begin(), name.end(), [](unsigned char value) { return value <= 32; }) ||
        !synchronize())
    {
        return false;
    }
    auto* tracked = state_->resolve(handle);
    if (!tracked)
    {
        return false;
    }
    tracked->reference->SetClass(std::string(name), active);
    return true;
}
bool UiSystem::set_style(UiElementHandle handle, std::string_view name, std::string_view value)
{
    if (name.empty() || !valid_string(name) || !valid_string(value) || !synchronize())
    {
        return false;
    }
    auto* tracked = state_->resolve(handle);
    return tracked && tracked->reference->SetProperty(std::string(name), std::string(value));
}
bool UiSystem::set_image(UiElementHandle handle, std::string_view path)
{
    if (path.empty() || !valid_string(path) || !synchronize())
    {
        return false;
    }
    State::Document* document = nullptr;
    auto* tracked = state_->resolve(handle, &document);
    if (!tracked || tracked->reference->GetTagName() != "img")
    {
        return false;
    }
    std::string guid;
    if (!state_->renderer.texture_guid(document->key, path, guid))
    {
        return false;
    }
    tracked->reference->SetAttribute("src", guid);
    return true;
}
void UiSystem::release_scene() noexcept
{
    if (!state_ || !state_->initialized)
    {
        return;
    }
    for (auto& document : state_->documents)
    {
        state_->close(document);
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
    if (!state_->initialized || SDL_GetWindowFromEvent(&event) != window_.native_window())
    {
        return false;
    }
    if (!synchronize() || state_->documents.empty())
    {
        return false;
    }
    const auto process = [&](State::Document& document)
    {
        if (!document.visible || !state_->activate(document))
        {
            return false;
        }
        state_->input_event =
            event.type == SDL_EVENT_TEXT_INPUT || event.type == SDL_EVENT_KEY_DOWN ||
            event.type == SDL_EVENT_KEY_UP || event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
            event.type == SDL_EVENT_MOUSE_BUTTON_UP || event.type == SDL_EVENT_MOUSE_MOTION ||
            event.type == SDL_EVENT_MOUSE_WHEEL;
        const bool consumed =
            ui_detail::process_input(*document.context, window_.native_window(), event);
        state_->input_event = false;
        return consumed;
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
            const auto visible =
                std::find_if(state_->documents.rbegin(), state_->documents.rend(),
                             [](const auto& document) { return document.visible; });
            if (visible == state_->documents.rend())
            {
                return false;
            }
            if (visible->context->GetFocusElement() == visible->context->GetRootElement() &&
                !visible->markup->Focus())
            {
                return false;
            }
            state_->keyboard_context = visible->context;
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
    if (!synchronize())
    {
        return false;
    }
    while (!state_->events.empty())
    {
        auto event = std::move(state_->events.front());
        state_->events.pop_front();
        State::Document* document = nullptr;
        const auto* element = state_->resolve(event.target, &document);
        if (event.scene_id != state_->scene_id || !element || !document->visible ||
            document->node != event.node_id || !element->reference->IsVisible() ||
            !element_enabled(*element->reference.get()))
        {
            continue;
        }
        output = std::move(event);
        return true;
    }
    return false;
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
