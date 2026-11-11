#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "runtime/gameplay/gameplay_session.hpp"

#include "runtime/app/app.hpp"
#include "runtime/input/input_system.hpp"
#include "runtime/log/log_system.hpp"
#include "runtime/navigation/navigation_system.hpp"
#include "runtime/resource/types/scene_resource.hpp"
#include "runtime/scene/animator.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/world/world_system.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace engine::plugins::script::bindings
{
BindingContext::BindingContext(ResourceLoader* resources, WorldSystem* world,
                               RenderSystem* renderer, UiSystem* ui, PhysicsSystem* physics,
                               NavigationSystem* navigation, InputSystem* input,
                               WindowSystem* window, MoviePlayer* movies) noexcept
    : resources_(resources), world_(world), renderer_(renderer), ui_(ui), physics_(physics),
      navigation_(navigation), input_(input), window_(window), movies_(movies)
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
    if (gameplay_)
    {
        for (const auto& [token, request] : scene_requests_)
        {
            (void)token;
            (void)gameplay_->cancel_scene_load(request);
            (void)gameplay_->release_scene_load(request);
        }
    }
    scene_requests_.clear();
    resource_requests_.clear();
    resource_handles_.clear();
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

void BindingContext::set_gameplay_session(GameplaySession* session) noexcept
{
    gameplay_ = session;
}
GameplaySession* BindingContext::gameplay() const noexcept
{
    return active_ && callable_ ? gameplay_ : nullptr;
}
std::uint64_t BindingContext::begin_scene_load(const SceneLoadSpec& spec)
{
    if (!gameplay() || resource_sequence_ == UINT64_MAX)
    {
        return 0;
    }
    const auto* current = world_ ? world_->scene() : nullptr;
    if (current && current->id() != scene_id_)
    {
        return 0;
    }
    SceneLoadId id;
    if (!gameplay_->begin_scene_load(spec, id))
    {
        return 0;
    }
    const auto token = ++resource_sequence_;
    scene_requests_.emplace(token, id);
    return token;
}
SceneLoadId BindingContext::scene_request(std::uint64_t token) const noexcept
{
    const auto found = scene_requests_.find(token);
    return gameplay() && found != scene_requests_.end() ? found->second : SceneLoadId{};
}
bool BindingContext::release_scene_request(std::uint64_t token)
{
    const auto id = scene_request(token);
    if (!gameplay() || id.request == 0 || !gameplay_->release_scene_load(id))
    {
        return false;
    }
    scene_requests_.erase(token);
    return true;
}
Scene* BindingContext::owned_scene(std::uint64_t id) const noexcept
{
    if (!world_ || !active_ || !callable_)
    {
        return nullptr;
    }
    if (gameplay_)
    {
        for (const auto& [token, request] : scene_requests_)
        {
            (void)token;
            auto* candidate = gameplay_->scene_load_scene(request);
            if (candidate && candidate->id() == id)
            {
                return candidate;
            }
        }
    }
    if (std::find(owned_scene_ids_.begin(), owned_scene_ids_.end(), id) != owned_scene_ids_.end())
    {
        for (const auto& scene : world_->scenes())
        {
            if (scene->id() == id)
            {
                return scene.get();
            }
        }
    }
    return nullptr;
}

std::uint64_t BindingContext::load_resource(std::string_view path)
{
    if (!active_ || !callable_ || !resources_ || resource_sequence_ == UINT64_MAX)
    {
        resource_error_ = "Resource service is unavailable";
        return 0;
    }
    ResourceHandle handle;
    if (!resources_->load(path, handle))
    {
        resource_error_ = resources_->last_error();
        return 0;
    }
    ResourceReference reference;
    if (!reference.adopt(resources_, handle))
    {
        (void)resources_->release(handle);
        return 0;
    }
    const auto token = ++resource_sequence_;
    resource_handles_.emplace(token, std::move(reference));
    resource_error_.clear();
    return token;
}

ResourceHandle BindingContext::resource_handle(std::uint64_t token) const noexcept
{
    const auto found = resource_handles_.find(token);
    return found != resource_handles_.end() ? found->second.handle() : ResourceHandle{};
}

std::uint64_t BindingContext::retain_resource(std::uint64_t token)
{
    ResourceReference reference;
    if (!active_ || !callable_ || resource_sequence_ == UINT64_MAX ||
        !reference.retain(resources_, resource_handle(token)))
    {
        resource_error_ = "Invalid resource acquisition";
        return 0;
    }
    const auto result = ++resource_sequence_;
    resource_handles_.emplace(result, std::move(reference));
    resource_error_.clear();
    return result;
}

bool BindingContext::release_resource(std::uint64_t token)
{
    if (!active_ || !callable_ || !resource_handles_.erase(token))
    {
        resource_error_ = "Invalid resource acquisition";
        return false;
    }
    resource_error_.clear();
    return true;
}

std::uint64_t BindingContext::begin_resource_load(std::string_view path)
{
    if (!active_ || !callable_ || !resources_ || resource_sequence_ == UINT64_MAX)
    {
        resource_error_ = "Resource service is unavailable";
        return 0;
    }
    std::unique_ptr<ResourceLoad> operation;
    if (!resources_->begin_load(path, operation))
    {
        resource_error_ = resources_->last_error();
        return 0;
    }
    const auto token = ++resource_sequence_;
    resource_requests_.emplace(token, std::move(operation));
    resource_error_.clear();
    return token;
}

ResourceLoad* BindingContext::resource_request(std::uint64_t token) const noexcept
{
    const auto found = resource_requests_.find(token);
    return found != resource_requests_.end() ? found->second.get() : nullptr;
}

std::uint64_t BindingContext::take_resource(std::uint64_t token)
{
    auto* operation = resource_request(token);
    if (!active_ || !callable_ || !operation || resource_sequence_ == UINT64_MAX)
    {
        return 0;
    }
    ResourceHandle handle;
    if (!operation->take(handle))
    {
        resource_error_ = operation->last_error();
        return 0;
    }
    ResourceReference reference;
    if (!reference.adopt(resources_, handle))
    {
        (void)resources_->release(handle);
        return 0;
    }
    const auto result = ++resource_sequence_;
    resource_handles_.emplace(result, std::move(reference));
    resource_requests_.erase(token);
    resource_error_.clear();
    return result;
}

bool BindingContext::cancel_resource_load(std::uint64_t token)
{
    auto* operation = resource_request(token);
    if (!active_ || !callable_ || !operation)
    {
        return false;
    }
    const bool cancelled = operation->cancel();
    resource_requests_.erase(token);
    return cancelled;
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
    auto* value = owned_scene(scene_id);
    return value ? value->find_node(node_id) : Node{};
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
        AnimatorPlaybackInfo info;
        if (animator && animator.active() &&
            animator.instance_id() == event.component_instance_id &&
            animator.get_playback_info(info) && info.playback_id == event.playback_id &&
            info.track_revision == event.track_revision &&
            info.animation_guid == event.animation_guid &&
            info.resource_generation == event.resource_generation)
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
    auto* scene = binding && identifier(arguments, 0, id) ? binding->owned_scene(id) : nullptr;
    if (!scene || !binding->world_ || binding->world_->scene() == scene)
    {
        return scene;
    }
    if (binding->gameplay_)
    {
        for (const auto& [token, request] : binding->scene_requests_)
        {
            (void)token;
            if (binding->gameplay_->scene_load_scene(request) == scene)
            {
                return scene;
            }
        }
    }
    return nullptr;
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
