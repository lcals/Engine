#include "runtime/ui/ui_system.hpp"
#include "runtime/render/frame_counters.hpp"
#include "runtime/resource/types/shader_resource.hpp"
#include "runtime/resource/types/texture_resource.hpp"
#include "runtime/resource/types/ui_resource.hpp"
#include "runtime/scene/ecs_scene.hpp"
#include "runtime/scene/ecs_storage.hpp"
#include "runtime/scene/scope_work.hpp"

#include "runtime/font/font_system.hpp"
#include "runtime/performance/profiling.hpp"
#include "runtime/resource/resource_reference.hpp"
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
#include <cmath>
#include <deque>
#include <limits>
#include <map>
#include <optional>
#include <set>

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

bool valid_pointer(float x, float y)
{
    constexpr float limit = 1000000.0F;
    return std::isfinite(x) && std::isfinite(y) && std::abs(x) <= limit && std::abs(y) <= limit;
}

bool interactive_element(Rml::Element& element)
{
    const auto& tag = element.GetTagName();
    if (tag == "button" || tag == "input" || tag == "textarea" || tag == "select" || tag == "a" ||
        tag == "label" || element.HasAttribute("data-input-capture"))
    {
        return true;
    }
    const auto& style = element.GetComputedValues();
    const auto scrollable = [](Rml::Style::Overflow value)
    { return value == Rml::Style::Overflow::Auto || value == Rml::Style::Overflow::Scroll; };
    return (scrollable(style.overflow_x()) &&
            element.GetScrollWidth() > element.GetClientWidth()) ||
           (scrollable(style.overflow_y()) &&
            element.GetScrollHeight() > element.GetClientHeight());
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
        std::uint64_t scene = 0;
        std::uint64_t node = 0;
        std::uint64_t view = 0;
        std::uint64_t revision = 0;
        std::uint64_t order = 0;
        std::uint64_t id = 0;
        ResourceReference resource;
        std::string key;
        Rml::Context* context = nullptr;
        Rml::ElementDocument* markup = nullptr;
        std::map<std::uint64_t, Element> elements;
        std::uint64_t element_sequence = 0;
        bool persistent = false;
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
    std::map<SceneScopeId, scene_detail::ScopeWork> preparing;
    std::deque<UiEvent> events;
    bool initialized = false;
    Rml::Context* keyboard_context = nullptr;
    Rml::Context* pointer_context = nullptr;
    Rml::ObserverPtr<Rml::Element> pointer_target;
    std::uint32_t pointer_buttons = 0;
    std::optional<std::pair<SDL_TouchID, SDL_FingerID>> touch_pointer;
    std::set<std::pair<SDL_TouchID, SDL_FingerID>> blocked_touches;
    std::set<SDL_Keycode> keys_down;
    int logical_width = 0;
    int logical_height = 0;
    bool canceling_input = false;
    bool input_event = false;
    Document* find_document(std::uint64_t node)
    {
        const auto found =
            std::find_if(documents.begin(), documents.end(),
                         [node](const auto& document) { return document.node == node; });
        return found == documents.end() ? nullptr : &*found;
    }
    bool prepare_document(WorldSystem& world, Node node, const ecs_detail::NodeRuntime& state,
                          bool& changed)
    {
        const auto* scene = node.scene();
        (void)scene;

        const auto ui = node.get_component<UiComponent>();
        if (!ui || ui.document().id == 0)
        {
            return true;
        }
        if (auto* document = this->find_document(node.id()))
        {
            document->order = state.order;
            document->persistent = node.persistent();
            return true;
        }
        State::Document candidate;
        candidate.scene = scene->id();
        candidate.node = node.id();
        candidate.view = ui.instance_id();
        candidate.revision = ui.document_revision();
        candidate.id = next_document_id();
        candidate.order = state.order;
        candidate.persistent = node.persistent();
        if (!candidate.resource.retain(world.resource_loader(), ui.document()))
        {
            return false;
        }
        candidate.key = candidate.resource->info().guid + "-" +
                        std::to_string(candidate.resource->generation());
        if (candidate.id == 0 || !this->activate(candidate))
        {
            this->prune();
            return false;
        }
        const auto name = "scene-ui-" + std::to_string(candidate.id);
        candidate.context = Rml::CreateContext(name, {logical_width, logical_height});
        if (!candidate.context)
        {
            this->prune();
            return false;
        }
        candidate.markup = candidate.context->LoadDocumentFromMemory(
            candidate.resource.data<UiResource>()->markup,
            "engine-ui/" + candidate.key + "/" + candidate.resource->info().path);
        if (!candidate.markup)
        {
            this->close(candidate);
            this->prune();
            return false;
        }
        candidate.markup->AddEventListener("click", this);
        candidate.markup->AddEventListener("change", this);
        candidate.markup->AddEventListener("input", this);
        candidate.visible = false;
        this->documents.push_back(std::move(candidate));
        changed = true;

        return true;
    }
    bool activate(Document& document)
    {
        return font_engine.activate(document.key, document.resource) &&
               renderer.activate(document.key, document.resource);
    }
    void cancel_pointer()
    {
        canceling_input = true;
        for (auto& document : documents)
        {
            if (document.context != nullptr)
            {
                ui_detail::cancel_pointer(
                    *document.context, document.context == pointer_context ? pointer_buttons : 0);
            }
        }
        canceling_input = false;
        pointer_context = nullptr;
        pointer_target.reset();
        pointer_buttons = 0;
        touch_pointer.reset();
        blocked_touches.clear();
    }
    Rml::Element* hit_target(Document& document, float x, float y)
    {
        if (!document.visible || !valid_pointer(x, y))
        {
            return nullptr;
        }
        if (document.markup->IsModal())
        {
            return document.markup;
        }
        auto* target = document.context->GetElementAtPoint({x, y});
        for (auto* current = target;
             current != nullptr && current != document.context->GetRootElement();
             current = current->GetParentNode())
        {
            if (current->GetOwnerDocument() == document.markup && current->IsVisible(true) &&
                interactive_element(*current))
            {
                return current;
            }
        }
        return nullptr;
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
            cancel_pointer();
        }
        clear_events(document.id);
        document.elements.clear();
        static_cast<void>(Rml::RemoveContext(document.context->GetName()));
        document.context = nullptr;
        document.markup = nullptr;
    }
    void ProcessEvent(Rml::Event& event) override
    {
        if (canceling_input)
        {
            return;
        }
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
                output.scene_id = document.scene;
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
SceneWorkState UiSystem::prepare_scope(SceneScopeId scope, const SceneWorkBudget& budget)
{
    if (!state_->initialized || !world_.refresh_scene_state())
    {
        return SceneWorkState::failed;
    }
    if (!window_.can_render())
    {
        return SceneWorkState::working;
    }
    int width = 0, height = 0, pixel_width = 0, pixel_height = 0;
    if (!SDL_GetWindowSize(window_.native_window(), &width, &height) ||
        !SDL_GetWindowSizeInPixels(window_.native_window(), &pixel_width, &pixel_height))
    {
        return SceneWorkState::failed;
    }
    state_->logical_width = width;
    state_->logical_height = height;
    std::erase_if(state_->preparing,
                  [&](const auto& entry) { return !world_.scope_candidate(entry.first); });
    auto found = state_->preparing.find(scope);
    if (found == state_->preparing.end())
    {
        scene_detail::ScopeWork work;
        if (!scene_detail::begin_work(world_, scope, "ui", work))
        {
            return SceneWorkState::failed;
        }
        found = state_->preparing.emplace(scope, std::move(work)).first;
    }
    return scene_detail::step_work(
        found->second, budget,
        [&](Node node)
        {
            const auto ui = node.get_component<UiComponent>();
            if (!ui || !ui.active() || ui.document().id == 0)
            {
                return true;
            }
            bool changed = false;
            const auto* runtime = flecs::world(world_.native_world())
                                      .entity(node.id())
                                      .try_get<ecs_detail::NodeRuntime>();
            if (!runtime || !state_->prepare_document(world_, node, *runtime, changed))
            {
                return false;
            }
            auto* document = state_->find_document(node.id());
            if (!document || !state_->activate(*document) ||
                !state_->renderer.begin(static_cast<std::uint32_t>(pixel_width),
                                        static_cast<std::uint32_t>(pixel_height), width, height))
            {
                return false;
            }
            state_->renderer.set_preparing(true);
            document->markup->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
            const bool ready = document->context->Update() && document->context->Render() &&
                               state_->renderer.healthy();
            if (!document->visible)
            {
                document->markup->Hide();
            }
            state_->renderer.set_preparing(false);
            return ready;
        });
}
bool UiSystem::scope_status(SceneScopeId scope, ScenePreparationSnapshot& output) const
{
    output = {};
    if (!state_)
    {
        return false;
    }
    const auto found = state_->preparing.find(scope);
    if (found == state_->preparing.end())
    {
        return false;
    }
    output = found->second.snapshot;
    return true;
}
void UiSystem::release_scope(SceneScopeId scope) noexcept
{
    if (!state_)
    {
        return;
    }
    const auto found = state_->preparing.find(scope);
    if (found == state_->preparing.end())
    {
        return;
    }
    for (auto node : found->second.nodes)
    {
        std::erase_if(state_->documents,
                      [&](auto& document)
                      {
                          if (document.node != node.id())
                          {
                              return false;
                          }
                          state_->close(document);
                          return true;
                      });
    }
    state_->preparing.erase(found);
}

bool UiSystem::synchronize()
{
    ENGINE_PROFILE_ZONE("Ui.synchronize");
    if (!state_->initialized)
    {
        return false;
    }
    if (world_.scenes().empty() && state_->preparing.empty())
    {
        if (!state_->documents.empty())
        {
            release_scene();
        }
        return true;
    }
    if (!world_.refresh_scene_state())
    {
        return false;
    }
    int logical_width = 0;
    int logical_height = 0;
    if (!SDL_GetWindowSize(window_.native_window(), &logical_width, &logical_height) ||
        logical_width <= 0 || logical_height <= 0)
    {
        return false;
    }
    if (state_->logical_width != 0 &&
        (state_->logical_width != logical_width || state_->logical_height != logical_height))
    {
        cancel_input();
    }
    state_->logical_width = logical_width;
    state_->logical_height = logical_height;
    const auto wanted = [&](const State::Document& document)
    {
        auto node = world_.find_node(document.node);
        if (!node)
        {
            for (const auto& [scope, work] : state_->preparing)
            {
                (void)work;
                auto* candidate = world_.scope_candidate(scope);
                if (candidate)
                {
                    node = candidate->find_node(document.node);
                }
                if (node)
                {
                    break;
                }
            }
        }
        const auto ui = node ? node.get_component<UiComponent>() : UiComponent{};
        const Resource* current = nullptr;
        return ui && world_.resource_loader() &&
               world_.resource_loader()->get(ui.document(), current) == ResourceError::none &&
               node.scene()->id() == document.scene && ui.instance_id() == document.view &&
               ui.document_revision() == document.revision && current == document.resource.get();
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
    {
        auto query = flecs::world(world_.native_world())
                         .query_builder<const ecs_detail::ComponentStorage<UiComponent::Data>,
                                        const ecs_detail::NodeRuntime>()
                         .with<ecs_detail::Published>()
                         .build();
        bool query_success = true;
        query.each(
            [&](flecs::entity entity, const ecs_detail::ComponentStorage<UiComponent::Data>& stored,
                const ecs_detail::NodeRuntime& state)
            {
                if (query_success)
                {
                    (void)stored;
                    query_success = state_->prepare_document(world_, world_.find_node(entity.id()),
                                                             state, changed);
                }
            });
        if (!query_success)
        {
            return false;
        }
    }
    if (changed)
    {
        state_->prune();
    }
    std::stable_sort(state_->documents.begin(), state_->documents.end(),
                     [](const auto& left, const auto& right)
                     {
                         return left.persistent != right.persistent ? !left.persistent
                                : left.scene != right.scene         ? left.scene < right.scene
                                                                    : left.order < right.order;
                     });
    for (auto& document : state_->documents)
    {
        const auto node = world_.find_node(document.node);
        const auto ui = node ? node.get_component<UiComponent>() : UiComponent{};
        const bool active = ui && ui.active();
        const bool visible = active && ui.visible_in_hierarchy();
        if (document.visible != visible)
        {
            document.visible = visible;
            if (visible)
            {
                document.markup->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
            }
            else
            {
                if (state_->pointer_context == document.context)
                {
                    state_->cancel_pointer();
                }
                document.markup->Hide();
                state_->clear_events(document.id);
                if (state_->keyboard_context == document.context)
                {
                    state_->keyboard_context = nullptr;
                    if (SDL_TextInputActive(window_.native_window()))
                    {
                        static_cast<void>(SDL_StopTextInput(window_.native_window()));
                    }
                }
            }
        }
        document.context->SetDimensions({logical_width, logical_height});
        if (active && (!state_->activate(document) || !document.context->Update()))
        {
            return false;
        }
    }
    if (state_->pointer_context != nullptr &&
        (!state_->pointer_target || !state_->pointer_target->IsVisible(true) ||
         !element_enabled(*state_->pointer_target.get())))
    {
        state_->cancel_pointer();
    }
    for (auto it = state_->documents.rbegin(); it != state_->documents.rend(); ++it)
    {
        if (it->visible && it->markup->IsModal())
        {
            if (state_->pointer_context != nullptr && state_->pointer_context != it->context)
            {
                state_->cancel_pointer();
            }
            break;
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
    const render_detail::UiDrawScope counters;
    ENGINE_PROFILE_ZONE("Ui.render");
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

bool UiSystem::document_root(std::uint64_t node, UiElementHandle& output)
{
    output = {};
    if (!synchronize())
    {
        return false;
    }
    auto* document = state_->find_document(node);
    if (!document)
    {
        return false;
    }
    output = state_->handle(*document, *document->markup);
    return output.element_id != 0;
}

bool UiSystem::inspect_element(UiElementHandle handle, UiElementInfo& output)
{
    output = {};
    State::Document* document = nullptr;
    auto* tracked = synchronize() ? state_->resolve(handle, &document) : nullptr;
    if (!tracked)
    {
        return false;
    }
    auto& element = *tracked->reference.get();
    output.handle = handle;
    if (auto* parent = element.GetParentNode();
        parent != nullptr && parent->GetOwnerDocument() == document->markup)
    {
        output.parent = state_->handle(*document, *parent);
    }
    output.tag = element.GetTagName();
    output.id = element.GetId();
    output.classes = element.GetClassNames();
    append_text(element, output.text);
    output.value = element_value(element);
    if (element.GetTagName() == "input" &&
        element.GetAttribute<Rml::String>("type", "") == "password")
    {
        output.value.assign(output.value.size(), '*');
    }
    output.child_count = static_cast<std::uint32_t>(element.GetNumChildren());
    output.visible = document->visible && element.IsVisible(true);
    output.enabled = element_enabled(element);
    output.focused = state_->keyboard_context == document->context &&
                     document->context->GetFocusElement() == &element;
    Rml::Rectanglef bounds;
    if (Rml::ElementUtilities::GetBoundingBox(bounds, &element, Rml::BoxArea::Border))
    {
        output.bounds = {bounds.Left(), bounds.Top(), bounds.Width(), bounds.Height()};
    }
    return true;
}

bool UiSystem::child_element(UiElementHandle parent, std::uint32_t index, UiElementHandle& output)
{
    output = {};
    State::Document* document = nullptr;
    auto* tracked = synchronize() ? state_->resolve(parent, &document) : nullptr;
    if (!tracked || index >= static_cast<std::uint32_t>(tracked->reference->GetNumChildren()))
    {
        return false;
    }
    auto* child = tracked->reference->GetChild(static_cast<int>(index));
    if (!child)
    {
        return false;
    }
    output = state_->handle(*document, *child);
    return output.element_id != 0;
}

bool UiSystem::element_style(UiElementHandle handle, std::string_view name, std::string& output)
{
    output.clear();
    if (name.empty() || !valid_string(name))
    {
        return false;
    }
    auto* tracked = synchronize() ? state_->resolve(handle) : nullptr;
    const auto* property = tracked ? tracked->reference->GetProperty(std::string(name)) : nullptr;
    if (!property)
    {
        return false;
    }
    output = property->ToString();
    return true;
}

bool UiSystem::hit_test(float x, float y, UiElementHandle& output)
{
    output = {};
    if (!valid_pointer(x, y) || !synchronize())
    {
        return false;
    }
    if (x < 0 || y < 0 || x >= state_->logical_width || y >= state_->logical_height)
    {
        return true;
    }
    for (auto it = state_->documents.rbegin(); it != state_->documents.rend(); ++it)
    {
        auto* claimed = state_->hit_target(*it, x, y);
        if (!claimed)
        {
            continue;
        }
        auto* target = it->context->GetElementAtPoint({x, y});
        if (!target || target->GetOwnerDocument() != it->markup)
        {
            target = claimed;
        }
        output = state_->handle(*it, *target);
        return output.element_id != 0;
    }
    return true;
}

bool UiSystem::pointer_active() const noexcept
{
    return state_->initialized && (state_->pointer_context != nullptr || state_->touch_pointer ||
                                   !state_->blocked_touches.empty());
}

bool UiSystem::focus_element(UiElementHandle handle)
{
    State::Document* document = nullptr;
    auto* tracked = synchronize() ? state_->resolve(handle, &document) : nullptr;
    if (!tracked || !document->visible || !tracked->reference->IsVisible(true) ||
        !element_enabled(*tracked->reference.get()) || !state_->activate(*document))
    {
        return false;
    }
    for (auto it = state_->documents.rbegin(); it != state_->documents.rend(); ++it)
    {
        if (it->visible && it->markup->IsModal() && it->context != document->context)
        {
            return false;
        }
        if (it->context == document->context)
        {
            break;
        }
    }
    if (!tracked->reference->Focus())
    {
        return false;
    }
    for (auto& other : state_->documents)
    {
        if (other.context != document->context)
        {
            if (auto* focused = other.context->GetFocusElement())
            {
                focused->Blur();
            }
        }
    }
    state_->keyboard_context = document->context;
    const auto& tag = tracked->reference->GetTagName();
    if ((tag == "input" || tag == "textarea") && !SDL_TextInputActive(window_.native_window()))
    {
        static_cast<void>(SDL_StartTextInput(window_.native_window()));
    }
    return true;
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
bool UiSystem::set_document_visible(std::uint64_t node_id, bool visible)
{
    auto node = world_.find_node(node_id);
    auto ui = node ? node.get_component<UiComponent>() : UiComponent{};
    if (!ui)
    {
        return false;
    }
    return ui.set_visible(visible) && synchronize();
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
void UiSystem::release_gpu() noexcept
{
    if (!state_ || !state_->initialized)
    {
        return;
    }
    cancel_input();
    Rml::ReleaseCompiledGeometry(&state_->renderer);
    Rml::ReleaseTextures(&state_->renderer);
    state_->renderer.release();
}

void UiSystem::release_scene() noexcept
{
    if (!state_ || !state_->initialized)
    {
        return;
    }
    cancel_input();
    for (auto& document : state_->documents)
    {
        state_->close(document);
    }
    state_->keyboard_context = nullptr;
    state_->pointer_context = nullptr;
    state_->pointer_target.reset();
    state_->pointer_buttons = 0;
    state_->touch_pointer.reset();
    state_->blocked_touches.clear();
    state_->documents.clear();
    state_->events.clear();
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
    return process_event(event, event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP
                                    ? event.key.mod
                                    : SDL_GetModState());
}

bool UiSystem::process_event(const SDL_Event& event, std::uint16_t modifiers)
{
    if (!state_->initialized)
    {
        return false;
    }
    if (event.type == SDL_EVENT_WILL_ENTER_BACKGROUND ||
        event.type == SDL_EVENT_DID_ENTER_BACKGROUND)
    {
        cancel_input();
        return false;
    }
    if (SDL_GetWindowFromEvent(&event) != window_.native_window())
    {
        return false;
    }
    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST || event.type == SDL_EVENT_WINDOW_RESIZED ||
        event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
        event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED)
    {
        cancel_input();
        return false;
    }
    if (((event.type == SDL_EVENT_MOUSE_MOTION && event.motion.which == SDL_TOUCH_MOUSEID) ||
         ((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) &&
          event.button.which == SDL_TOUCH_MOUSEID)) ||
        ((event.type == SDL_EVENT_FINGER_DOWN || event.type == SDL_EVENT_FINGER_MOTION ||
          event.type == SDL_EVENT_FINGER_UP || event.type == SDL_EVENT_FINGER_CANCELED) &&
         event.tfinger.touchID == SDL_MOUSE_TOUCHID))
    {
        return false;
    }
    if (!synchronize() || state_->documents.empty())
    {
        return false;
    }
    const auto process = [&](State::Document& document, const SDL_Event& input)
    {
        if (!document.visible || !state_->activate(document))
        {
            return false;
        }
        state_->input_event =
            input.type == SDL_EVENT_TEXT_INPUT || input.type == SDL_EVENT_KEY_DOWN ||
            input.type == SDL_EVENT_KEY_UP || input.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
            input.type == SDL_EVENT_MOUSE_BUTTON_UP || input.type == SDL_EVENT_MOUSE_MOTION ||
            input.type == SDL_EVENT_MOUSE_WHEEL;
        const bool consumed =
            ui_detail::process_input(*document.context, window_.native_window(), input, modifiers);
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
    const bool touch = event.type == SDL_EVENT_FINGER_DOWN ||
                       event.type == SDL_EVENT_FINGER_MOTION || event.type == SDL_EVENT_FINGER_UP ||
                       event.type == SDL_EVENT_FINGER_CANCELED;
    if (touch)
    {
        const auto identity = std::pair{event.tfinger.touchID, event.tfinger.fingerID};
        const bool terminal =
            event.type == SDL_EVENT_FINGER_UP || event.type == SDL_EVENT_FINGER_CANCELED;
        if (const auto blocked = state_->blocked_touches.find(identity);
            blocked != state_->blocked_touches.end())
        {
            if (terminal)
            {
                state_->blocked_touches.erase(blocked);
            }
            return true;
        }
        const bool captured = state_->touch_pointer == identity;
        if (!captured && event.type != SDL_EVENT_FINGER_DOWN)
        {
            return false;
        }
        if (captured && event.type == SDL_EVENT_FINGER_CANCELED)
        {
            state_->cancel_pointer();
            return true;
        }
        const float x = event.tfinger.x * static_cast<float>(state_->logical_width);
        const float y = event.tfinger.y * static_cast<float>(state_->logical_height);
        if (!valid_pointer(x, y))
        {
            if (captured)
            {
                state_->cancel_pointer();
            }
            return captured;
        }
        State::Document* selected = nullptr;
        Rml::Element* target = nullptr;
        for (auto it = state_->documents.rbegin(); it != state_->documents.rend(); ++it)
        {
            if (captured ? it->context == state_->pointer_context
                         : (target = state_->hit_target(*it, x, y)) != nullptr)
            {
                selected = &*it;
                break;
            }
        }
        if (selected == nullptr)
        {
            if (!captured)
            {
                select_focus(nullptr);
            }
            return false;
        }
        if (!captured && state_->pointer_context != nullptr)
        {
            // One RmlUi pointer; other UI touches remain claimed without driving it.
            state_->blocked_touches.insert(identity);
            return true;
        }
        SDL_Event pointer{};
        pointer.type = SDL_EVENT_MOUSE_MOTION;
        pointer.motion.timestamp = event.tfinger.timestamp;
        pointer.motion.windowID = event.tfinger.windowID;
        pointer.motion.x = x;
        pointer.motion.y = y;
        pointer.motion.state = captured ? SDL_BUTTON_LMASK : 0;
        static_cast<void>(process(*selected, pointer));
        if ((!captured && event.type == SDL_EVENT_FINGER_DOWN) || event.type == SDL_EVENT_FINGER_UP)
        {
            pointer = {};
            pointer.type = terminal ? SDL_EVENT_MOUSE_BUTTON_UP : SDL_EVENT_MOUSE_BUTTON_DOWN;
            pointer.button.timestamp = event.tfinger.timestamp;
            pointer.button.windowID = event.tfinger.windowID;
            pointer.button.button = SDL_BUTTON_LEFT;
            pointer.button.down = !terminal;
            pointer.button.clicks = 1;
            pointer.button.x = x;
            pointer.button.y = y;
            static_cast<void>(process(*selected, pointer));
        }
        if (!captured)
        {
            select_focus(selected->context);
            state_->pointer_context = selected->context;
            state_->pointer_target = target->GetObserverPtr();
            state_->pointer_buttons = SDL_BUTTON_LMASK;
            state_->touch_pointer = identity;
        }
        else if (terminal)
        {
            state_->pointer_context = nullptr;
            state_->pointer_target.reset();
            state_->pointer_buttons = 0;
            state_->touch_pointer.reset();
            static_cast<void>(selected->context->ProcessMouseLeave());
        }
        return true;
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
                if (event.type == SDL_EVENT_KEY_DOWN)
                {
                    state_->keys_down.insert(event.key.key);
                }
                else if (event.type == SDL_EVENT_KEY_UP)
                {
                    state_->keys_down.erase(event.key.key);
                }
                return process(document, event);
            }
        }
        return false;
    }
    const bool mouse =
        event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
        event.type == SDL_EVENT_MOUSE_BUTTON_UP || event.type == SDL_EVENT_MOUSE_WHEEL;
    if (!mouse || state_->touch_pointer)
    {
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
         event.type == SDL_EVENT_MOUSE_BUTTON_UP || event.type == SDL_EVENT_MOUSE_WHEEL))
    {
        for (auto& document : state_->documents)
        {
            if (document.context == state_->pointer_context)
            {
                static_cast<void>(process(document, event));
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
                        state_->pointer_target.reset();
                    }
                }
                return true;
            }
        }
    }
    float x = 0;
    float y = 0;
    if (event.type == SDL_EVENT_MOUSE_MOTION)
    {
        x = event.motion.x;
        y = event.motion.y;
    }
    else if (event.type == SDL_EVENT_MOUSE_WHEEL)
    {
        x = event.wheel.mouse_x;
        y = event.wheel.mouse_y;
    }
    else
    {
        x = event.button.x;
        y = event.button.y;
    }
    if (!valid_pointer(x, y))
    {
        return false;
    }
    for (auto it = state_->documents.rbegin(); it != state_->documents.rend(); ++it)
    {
        auto* target = state_->hit_target(*it, x, y);
        if (event.type == SDL_EVENT_MOUSE_MOTION || target != nullptr)
        {
            static_cast<void>(process(*it, event));
        }
        if (target != nullptr)
        {
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
            {
                select_focus(it->context);
                state_->pointer_context = it->context;
                state_->pointer_target = target->GetObserverPtr();
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
bool UiSystem::keyboard_claimed() const noexcept
{
    if (!state_->initialized)
    {
        return false;
    }
    for (const auto& document : state_->documents)
    {
        if (!document.visible)
        {
            continue;
        }
        if (document.markup->IsModal())
        {
            return true;
        }
        auto* focused = document.context->GetFocusElement();
        if (document.context == state_->keyboard_context && focused != nullptr &&
            focused->IsVisible(true) && element_enabled(*focused) && form_control(*focused))
        {
            return true;
        }
    }
    return false;
}
bool UiSystem::pointer_claimed() const noexcept
{
    return state_->initialized &&
           std::any_of(state_->documents.begin(), state_->documents.end(), [](const auto& document)
                       { return document.visible && document.markup->IsModal(); });
}
bool UiSystem::owns_pointer(const SDL_Event& event) const noexcept
{
    if (!state_->initialized || SDL_GetWindowFromEvent(&event) != window_.native_window())
    {
        return false;
    }
    if (event.type == SDL_EVENT_FINGER_DOWN || event.type == SDL_EVENT_FINGER_MOTION ||
        event.type == SDL_EVENT_FINGER_UP || event.type == SDL_EVENT_FINGER_CANCELED)
    {
        const auto identity = std::pair{event.tfinger.touchID, event.tfinger.fingerID};
        return state_->touch_pointer == identity || state_->blocked_touches.contains(identity);
    }
    return state_->pointer_context != nullptr && !state_->touch_pointer &&
           (event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
            event.type == SDL_EVENT_MOUSE_BUTTON_UP || event.type == SDL_EVENT_MOUSE_WHEEL);
}
void UiSystem::cancel_input() noexcept
{
    if (!state_ || !state_->initialized)
    {
        return;
    }
    state_->cancel_pointer();
    state_->canceling_input = true;
    for (auto& document : state_->documents)
    {
        if (auto* focused = document.context->GetFocusElement())
        {
            focused->Blur();
        }
        for (const auto key : state_->keys_down)
        {
            SDL_Event release{};
            release.type = SDL_EVENT_KEY_UP;
            release.key.windowID = SDL_GetWindowID(window_.native_window());
            release.key.key = key;
            static_cast<void>(
                ui_detail::process_input(*document.context, window_.native_window(), release, 0));
        }
    }
    state_->canceling_input = false;
    state_->keys_down.clear();
    state_->keyboard_context = nullptr;
    if (SDL_TextInputActive(window_.native_window()))
    {
        static_cast<void>(SDL_StopTextInput(window_.native_window()));
    }
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
        if (!element || event.scene_id != document->scene || !document->visible ||
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
