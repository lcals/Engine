#include "plugins/runtime/script/bindings/binding_context.hpp"

#include "runtime/scene/animator_runtime.hpp"

#include "runtime/app/app.hpp"
#include "runtime/input/input_system.hpp"
#include "runtime/log/log_system.hpp"
#include "runtime/navigation/navigation_system.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/animator.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/world/world_system.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace engine::plugins::script::bindings
{
BindingContext::BindingContext(WorldSystem* world, RenderSystem* renderer, UiSystem* ui,
                               PhysicsSystem* physics, NavigationSystem* navigation,
                               InputSystem* input, WindowSystem* window,
                               MoviePlayer* movies) noexcept
    : world_(world), renderer_(renderer), ui_(ui), physics_(physics), navigation_(navigation),
      input_(input), window_(window), movies_(movies)
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

void BindingContext::cancel_movies() noexcept
{
    if (movies_ && movie_handle_.id)
    {
        MoviePlaybackStatus status;
        if (movies_->status(movie_handle_, status))
        {
            if (status.state == MovieState::preparing || status.state == MovieState::playing ||
                status.state == MovieState::paused)
            {
                (void)movies_->stop(movie_handle_, MovieStopReason::stopped);
            }
            (void)movies_->release(movie_handle_);
        }
    }
    movie_handle_ = {};
}

MoviePlayer* BindingContext::movies() const noexcept
{
    return movies_;
}
bool BindingContext::remember_movie(MoviePlaybackHandle handle) noexcept
{
    if (movie_handle_.id || !handle.id)
    {
        return false;
    }
    movie_handle_ = handle;
    return true;
}
bool BindingContext::owns_movie(MoviePlaybackHandle handle) const noexcept
{
    return handle.id && movie_handle_.id == handle.id &&
           movie_handle_.generation == handle.generation;
}
void BindingContext::forget_movie(MoviePlaybackHandle handle) noexcept
{
    if (owns_movie(handle))
    {
        movie_handle_ = {};
    }
}

void BindingContext::shutdown() noexcept
{
    cancel_movies();
    cancel_scene_load();
    async_results_.clear();
    animation_events_.clear();
    animation_event_cursor_ = 0;
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
    if (world_ != nullptr && world_->is_initialized())
    {
        const auto* current = world_->scene();
        if (navigation_ && current &&
            std::find(owned_scene_ids_.begin(), owned_scene_ids_.end(), current->id()) !=
                owned_scene_ids_.end())
        {
            navigation_->clear();
        }
        for (const auto id : owned_scene_ids_)
        {
            (void)world_->destroy_scene(id);
        }
    }
    owned_scene_ids_.clear();
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
        scene_load_error_ =
            loader.last_error().empty() ? "Scene step loading is unavailable" : loader.last_error();
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
    scene_load_status_ = ResourceLoadStatus::loading;
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
        if (!scene_load_context_valid())
        {
            fail_scene_load("The scene loading directory or owning world changed");
        }
        else
        {
            const auto resources_status = scene_load_->step(resource_budget, scene_load_progress_);
            if (resources_status == ResourceLoadStatus::failed)
            {
                fail_scene_load(scene_load_->last_error().empty() ? "Cannot load scene resources"
                                                                  : scene_load_->last_error());
            }
            else if (resources_status == ResourceLoadStatus::ready && resource_budget != 0)
            {
                if (scene_restore_request_ == 0)
                {
                    scene_restore_request_ = world_->begin_restore_scene(scene_load_->resource());
                }
                const auto restored =
                    world_->step_restore_scene(scene_restore_request_, resource_budget);
                if (restored == SceneRestoreStatus::failed)
                {
                    fail_scene_load("Cannot restore scene candidate: " + scene_load_path_);
                }
                else if (restored == SceneRestoreStatus::ready)
                {
                    scene_load_status_ = ResourceLoadStatus::ready;
                }
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
    if (!scene_load_context_valid())
    {
        fail_scene_load("Cannot commit scene after its directory or owning world changed");
        return nullptr;
    }
    // 候选已在主线程按预算恢复；提交只切换活动场景所有权。
    const auto root = scene_load_->resource();
    if (!root || !root->scene() || root->generation() != scene_load_generation_)
    {
        fail_scene_load("The prepared scene resource is unavailable");
        return nullptr;
    }
    auto* scene = world_->commit_restore_scene(scene_restore_request_);
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
    if (world_ != nullptr && scene_restore_request_ != 0)
    {
        world_->cancel_restore_scene(scene_restore_request_);
    }
    scene_restore_request_ = 0;
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

bool BindingContext::scene_load_context_valid() const noexcept
{
    auto* resources = ResourceSystem::instance();
    if (resources == nullptr || resources != scene_load_resources_ ||
        resources->generation() != scene_load_generation_ ||
        &resources->loader() != scene_load_loader_ || world_ == nullptr ||
        !world_->is_initialized())
    {
        return false;
    }
    const auto* current = world_->scene();
    return (current != nullptr ? current->id() : 0) == scene_id_;
}

void BindingContext::fail_scene_load(std::string_view error)
{
    if (world_ != nullptr && scene_restore_request_ != 0)
    {
        world_->cancel_restore_scene(scene_restore_request_);
    }
    scene_restore_request_ = 0;
    scene_load_error_ = error;
    scene_load_status_ = ResourceLoadStatus::failed;
    scene_load_.reset();
}

std::uint64_t
BindingContext::submit_async(const std::function<bool(std::function<void(bool)>)>& start)
{
    if (!active_ || !callable_ || !start || async_results_.size() >= 128 ||
        async_sequence_ == std::numeric_limits<std::uint64_t>::max())
    {
        return 0;
    }
    auto result = std::make_shared<AsyncResult>();
    if (!start(
            [weak = std::weak_ptr<AsyncResult>(result)](bool success)
            {
                if (auto target = weak.lock())
                {
                    target->success = success;
                    target->ready = true;
                }
            }))
    {
        return 0;
    }
    const auto request = ++async_sequence_;
    async_results_.emplace(request, std::move(result));
    return request;
}

int BindingContext::take_async(std::uint64_t request)
{
    if (!active_ || !callable_)
    {
        return -2;
    }
    const auto* scene = world_ && world_->is_initialized() ? world_->scene() : nullptr;
    if (scene == nullptr || scene->id() != scene_id_)
    {
        // 原生宿主也可换场景；旧 Run 不能在新世界领取之前的委托。
        async_results_.clear();
        return -2;
    }
    const auto found = async_results_.find(request);
    if (found == async_results_.end())
    {
        return -2;
    }
    if (!found->second->ready)
    {
        return -1;
    }
    const bool success = found->second->success;
    async_results_.erase(found);
    return success ? 1 : 0;
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
        async_results_.clear();
        cancel_scene_load();
    }
    if (active_ && scene_id_ != value && navigation_)
    {
        navigation_->clear();
    }
    scene_id_ = value;
    if (world_)
    {
        std::erase_if(owned_scene_ids_,
                      [this](std::uint64_t id)
                      {
                          return std::none_of(world_->scenes().begin(), world_->scenes().end(),
                                              [id](const auto& scene)
                                              { return scene->id() == id; });
                      });
        if (value != 0 && world_->scene() && world_->scene()->id() == value &&
            std::find(owned_scene_ids_.begin(), owned_scene_ids_.end(), value) ==
                owned_scene_ids_.end())
        {
            owned_scene_ids_.push_back(value);
        }
    }
}

Node BindingContext::find_node(std::uint64_t scene_id, std::uint64_t node_id) const noexcept
{
    if (!active_ || !callable_ || !world_ || !world_->is_initialized() ||
        std::find(owned_scene_ids_.begin(), owned_scene_ids_.end(), scene_id) ==
            owned_scene_ids_.end())
    {
        return {};
    }
    auto node = world_->find_node(node_id);
    return node && node.scene()->id() == scene_id ? node : Node{};
}

bool BindingContext::push_animation_events(std::span<const AnimationEvent> events)
{
    constexpr std::size_t maximum_events = 4096;
    const auto pending = animation_events_.size() - animation_event_cursor_;
    if (!active_ || events.size() > maximum_events - pending)
    {
        return false;
    }
    if (animation_event_cursor_ != 0)
    {
        animation_events_.erase(animation_events_.begin(),
                                animation_events_.begin() + animation_event_cursor_);
        animation_event_cursor_ = 0;
    }
    animation_events_.insert(animation_events_.end(), events.begin(), events.end());
    return true;
}

bool BindingContext::take_animation_event(AnimationEvent& output)
{
    output = {};
    if (!active_ || !callable_)
    {
        return false;
    }
    while (animation_event_cursor_ < animation_events_.size())
    {
        const auto& event = animation_events_[animation_event_cursor_++];
        const auto node = find_node(event.scene_id, event.node_id);
        const auto animator = node ? node.get_component<Animator>() : Animator{};
        const auto* runtime = animator ? engine::ecs_detail::component_runtime(animator) : nullptr;
        if (animator && animator.active() &&
            animator.instance_id() == event.component_instance_id && runtime &&
            runtime->playback_id_ == event.playback_id &&
            runtime->track_revision_ == event.track_revision && runtime->animation_ &&
            runtime->animation_->info().guid == event.animation_guid &&
            runtime->animation_->generation() == event.resource_generation)
        {
            output = event;
            return true;
        }
    }
    animation_events_.clear();
    animation_event_cursor_ = 0;
    return false;
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

Node BindingContext::node(const Arguments& arguments)
{
    auto* binding = current(arguments);
    std::uint64_t scene_id = 0;
    std::uint64_t node_id = 0;
    return binding && identifier(arguments, 0, scene_id) && identifier(arguments, 1, node_id)
               ? binding->find_node(scene_id, node_id)
               : Node{};
}

Component BindingContext::component(const Arguments& arguments)
{
    auto value = node(arguments);
    if (!value || arguments.count() < 3 || !arguments.is_int32(2))
    {
        return {};
    }
    const auto kind = arguments.integer(2);
    if (kind <= 0 || kind > std::numeric_limits<std::uint8_t>::max())
    {
        return {};
    }
    return value.get_component(static_cast<ComponentType>(kind));
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
