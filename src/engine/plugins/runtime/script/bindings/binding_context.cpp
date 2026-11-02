#include "plugins/runtime/script/bindings/binding_context.hpp"

#include "runtime/app/app.hpp"
#include "runtime/input/input_system.hpp"
#include "runtime/log/log_system.hpp"
#include "runtime/navigation/navigation_system.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/world/world_system.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace engine::plugins::script::bindings
{
BindingContext::BindingContext(WorldSystem* world, RenderSystem* renderer, UiSystem* ui,
                               PhysicsSystem* physics, NavigationSystem* navigation,
                               InputSystem* input, WindowSystem* window) noexcept
    : world_(world), renderer_(renderer), ui_(ui), physics_(physics), navigation_(navigation),
      input_(input), window_(window)
{
}

BindingContext::~BindingContext() noexcept
{
    shutdown();
}

void BindingContext::activate() noexcept
{
    active_ = true;
}

void BindingContext::begin_call(double delta_time) noexcept
{
    callable_ = active_;
    delta_time_ = std::isfinite(delta_time) ? delta_time : 0.0;
}

void BindingContext::end_call() noexcept
{
    callable_ = false;
    delta_time_ = 0.0;
}

void BindingContext::shutdown() noexcept
{
    cancel_scene_load();
    if (restore_mouse_mode_ && window_ != nullptr && window_->is_initialized() &&
        !window_->set_mouse_mode(previous_mouse_mode_))
    {
        LogSystem::write(LogLevel::warning, "Cannot restore the previous script mouse mode");
    }
    restore_mouse_mode_ = false;
    active_ = false;
    end_call();
    if (input_ && input_->is_initialized())
    {
        for (const auto channel : input_channels_)
        {
            if (!input_->destroy_channel(channel))
            {
                LogSystem::write(LogLevel::warning, "Cannot release a script input channel");
            }
        }
    }
    input_channels_.clear();
    input_captures_.clear();
    input_error_.clear();
    if (world_ != nullptr && world_->scene() != nullptr && world_->scene()->id() == scene_id_)
    {
        if (navigation_)
        {
            navigation_->clear();
        }
        world_->destroy_scene();
    }
    scene_id_ = 0;
}

WorldSystem* BindingContext::world() const noexcept
{
    return world_;
}

RenderSystem* BindingContext::renderer() const noexcept
{
    return callable_ ? renderer_ : nullptr;
}

UiSystem* BindingContext::ui() const noexcept
{
    return callable_ ? ui_ : nullptr;
}

PhysicsSystem* BindingContext::physics() const noexcept
{
    return callable_ ? physics_ : nullptr;
}

NavigationSystem* BindingContext::navigation() const noexcept
{
    return callable_ ? navigation_ : nullptr;
}

InputSystem* BindingContext::input() const noexcept
{
    return callable_ ? input_ : nullptr;
}

WindowSystem* BindingContext::window() const noexcept
{
    return callable_ ? window_ : nullptr;
}

bool BindingContext::set_mouse_mode(MouseMode mode)
{
    if (!active_ || !callable_ || window_ == nullptr || !window_->is_initialized())
    {
        return false;
    }
    if (!restore_mouse_mode_)
    {
        if (!window_->mouse_mode(previous_mouse_mode_))
        {
            return false;
        }
        restore_mouse_mode_ = true;
    }
    return window_->set_mouse_mode(mode);
}

bool BindingContext::begin_scene_load(std::string_view path)
{
    if (!active_ || !callable_ || world_ == nullptr || !world_->is_initialized() ||
        !scene_load_path_.empty())
    {
        return false;
    }
    const auto* current = world_->scene();
    if ((current != nullptr ? current->id() : 0) != scene_id_)
    {
        return false;
    }
    scene_load_error_.clear();
    auto* resources = ResourceSystem::instance();
    Guid guid;
    if (resources == nullptr || !ResourceSystem::path_to_guid(path, guid))
    {
        scene_load_error_ = "Scene path is unavailable: " + std::string(path);
        return false;
    }
    const auto* entry = resources->catalog().find(guid);
    if (entry == nullptr || entry->kind != schema::AssetKind::scene)
    {
        scene_load_error_ = "The requested resource is not a scene";
        return false;
    }
    auto& loader = resources->loader();
    std::unique_ptr<ResourceLoad> operation;
    if (!loader.begin_load(guid, operation) || !operation)
    {
        scene_load_error_ = loader.last_error().empty() ? "Scene step loading is unavailable"
                                                       : loader.last_error();
        return false;
    }
    ResourceLoadProgress progress;
    const auto status = operation->step(0, progress);
    if (status == ResourceLoadStatus::failed || progress.total == 0)
    {
        scene_load_error_ = operation->last_error().empty() ? "Cannot prepare scene resources"
                                                          : operation->last_error();
        return false;
    }
    scene_load_ = std::move(operation);
    scene_load_resources_ = resources;
    scene_load_loader_ = &loader;
    scene_load_generation_ = resources->generation();
    scene_load_progress_ = progress;
    scene_load_status_ = status;
    scene_load_path_ = path;
    return true;
}

ResourceLoadStatus BindingContext::step_scene_load(std::uint32_t resource_budget,
                                                  ResourceLoadProgress& output)
{
    output = {};
    if (!active_ || !callable_ || scene_load_path_.empty())
    {
        return ResourceLoadStatus::failed;
    }
    if (scene_load_status_ == ResourceLoadStatus::loading)
    {
        auto* resources = ResourceSystem::instance();
        const auto* current = world_ != nullptr ? world_->scene() : nullptr;
        if (resources != scene_load_resources_ || resources == nullptr ||
            resources->generation() != scene_load_generation_ ||
            &resources->loader() != scene_load_loader_ || world_ == nullptr ||
            !world_->is_initialized() || (current != nullptr ? current->id() : 0) != scene_id_)
        {
            fail_scene_load("The scene loading directory or owning world changed");
        }
        else
        {
            scene_load_status_ = scene_load_->step(resource_budget, scene_load_progress_);
            if (scene_load_status_ == ResourceLoadStatus::failed)
            {
                fail_scene_load(scene_load_->last_error().empty() ? "Cannot load scene resources"
                                                                : scene_load_->last_error());
            }
        }
    }
    output = scene_load_progress_;
    return scene_load_status_;
}

Scene* BindingContext::commit_scene_load()
{
    if (!active_ || !callable_ || scene_load_path_.empty() ||
        scene_load_status_ != ResourceLoadStatus::ready)
    {
        return nullptr;
    }
    auto* resources = ResourceSystem::instance();
    const auto* current = world_ != nullptr ? world_->scene() : nullptr;
    if (resources == nullptr || resources != scene_load_resources_ ||
        resources->generation() != scene_load_generation_ ||
        &resources->loader() != scene_load_loader_ || world_ == nullptr ||
        !world_->is_initialized() || (current != nullptr ? current->id() : 0) != scene_id_)
    {
        fail_scene_load("Cannot commit scene after its directory or owning world changed");
        return nullptr;
    }
    // 保持完整闭包，直到既有 load_scene 成功后由 Scene 接管根引用。
    const auto root = scene_load_->resource();
    if (!root || !root->scene() || root->generation() != scene_load_generation_)
    {
        fail_scene_load("The prepared scene resource is unavailable");
        return nullptr;
    }
    auto* scene = world_->load_scene(scene_load_path_);
    if (scene == nullptr)
    {
        fail_scene_load("Cannot restore scene: " + scene_load_path_);
        return nullptr;
    }
    set_scene_id(scene->id());
    return scene;
}

void BindingContext::cancel_scene_load() noexcept
{
    scene_load_.reset();
    scene_load_resources_ = nullptr;
    scene_load_loader_ = nullptr;
    scene_load_progress_ = {};
    scene_load_status_ = ResourceLoadStatus::loading;
    scene_load_generation_ = 0;
    scene_load_path_.clear();
    scene_load_error_.clear();
}

std::string_view BindingContext::scene_load_error() const noexcept
{
    return scene_load_error_;
}

void BindingContext::fail_scene_load(std::string_view error)
{
    scene_load_error_ = error;
    scene_load_status_ = ResourceLoadStatus::failed;
    scene_load_.reset();
}

void BindingContext::remember_input_channel(schema::InputChannelHandle channel)
{
    input_channels_.push_back(channel);
}

bool BindingContext::owns_input_channel(schema::InputChannelHandle channel) const noexcept
{
    return std::any_of(
        input_channels_.begin(), input_channels_.end(), [channel](const auto& value)
        { return value.slot() == channel.slot() && value.generation() == channel.generation(); });
}

void BindingContext::forget_input_channel(schema::InputChannelHandle channel)
{
    const auto matches = [channel](const auto& value)
    { return value.slot() == channel.slot() && value.generation() == channel.generation(); };
    std::erase_if(input_channels_, matches);
    std::erase_if(input_captures_, [&matches](const auto& value) { return matches(value.first); });
}

void BindingContext::remember_input_capture(schema::InputChannelHandle channel,
                                            schema::InputCaptureHandle capture)
{
    std::erase_if(input_captures_,
                  [channel](const auto& value)
                  {
                      return value.first.slot() == channel.slot() &&
                             value.first.generation() == channel.generation();
                  });
    input_captures_.emplace_back(channel, capture);
}

bool BindingContext::owns_input_capture(schema::InputCaptureHandle capture) const noexcept
{
    return std::any_of(input_captures_.begin(), input_captures_.end(),
                       [capture](const auto& value)
                       {
                           return value.second.slot() == capture.slot() &&
                                  value.second.generation() == capture.generation();
                       });
}

void BindingContext::set_input_error(std::string_view error)
{
    input_error_ = error;
}

const std::string& BindingContext::input_error() const noexcept
{
    return input_error_;
}

std::uint64_t BindingContext::scene_id() const noexcept
{
    return scene_id_;
}

void BindingContext::set_scene_id(std::uint64_t value) noexcept
{
    if (scene_id_ != value)
    {
        cancel_scene_load();
    }
    if (active_ && scene_id_ != value && navigation_)
    {
        navigation_->clear();
    }
    scene_id_ = value;
}

double BindingContext::delta_time() const noexcept
{
    return callable_ ? delta_time_ : 0.0;
}

KeyValueStore& BindingContext::storage()
{
    if (!storage_attempted_)
    {
        storage_attempted_ = true;
        const auto directory = App::is_initialized() ? App::persistent_data_path() / "userdata"
                                                     : std::filesystem::path{};
        if (!storage_.load(directory))
        {
            return storage_;
        }
    }
    return storage_;
}

void BindingContext::set_storage_error(std::string_view error)
{
    storage_error_ = error;
}

const std::string& BindingContext::storage_error()
{
    return storage_error_.empty() ? storage().last_error() : storage_error_;
}

BindingContext* BindingContext::current(const Arguments& arguments, bool require_callable)
{
    auto* active = static_cast<BindingContext*>(arguments.owner());
    if (active == nullptr || !active->active_ || (require_callable && !active->callable_))
    {
        return nullptr;
    }
    return active;
}

Scene* BindingContext::scene(const Arguments& arguments)
{
    auto* binding = current(arguments);
    std::uint64_t id = 0;
    if (binding == nullptr || binding->world_ == nullptr || !binding->world_->is_initialized() ||
        !identifier(arguments, 0, id) || binding->scene_id_ == 0 || binding->scene_id_ != id)
    {
        return nullptr;
    }
    auto* value = binding->world_->scene();
    return value != nullptr && value->id() == id ? value : nullptr;
}

Node* BindingContext::node(const Arguments& arguments)
{
    auto* value = scene(arguments);
    std::uint64_t id = 0;
    return value != nullptr && identifier(arguments, 1, id) ? value->find_node(id) : nullptr;
}

Component* BindingContext::component(const Arguments& arguments)
{
    auto* value = node(arguments);
    if (value == nullptr || arguments.count() < 3 || !arguments.is_int32(2))
    {
        return nullptr;
    }
    const auto kind = arguments.integer(2);
    if (kind <= 0 || kind > std::numeric_limits<std::uint8_t>::max())
    {
        return nullptr;
    }
    return value->get_component(static_cast<ComponentType>(kind));
}

bool BindingContext::identifier(const Arguments& arguments, int index, std::uint64_t& output)
{
    output = 0;
    return arguments.identifier(index, output);
}

bool BindingContext::number(const Arguments& arguments, int index, double& output)
{
    output = 0.0;
    return arguments.number(index, output);
}

bool BindingContext::scalar(const Arguments& arguments, int index, float& output)
{
    output = 0.0F;
    double value = 0.0;
    if (!number(arguments, index, value) || std::abs(value) > std::numeric_limits<float>::max())
    {
        return false;
    }
    output = static_cast<float>(value);
    return true;
}

bool BindingContext::text(const Arguments& arguments, int index, std::string& output)
{
    output.clear();
    return arguments.text(index, output);
}

} // namespace engine::plugins::script::bindings
