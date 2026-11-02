#pragma once

#include "plugins/runtime/script/vm/script_vm.hpp"
#include "runtime/resource/resource_load.hpp"
#include "runtime/schema/input_generated.h"
#include "runtime/storage/key_value_store.hpp"
#include "runtime/window/window_system.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine
{
class WorldSystem;
class RenderSystem;
class UiSystem;
class PhysicsSystem;
class NavigationSystem;
class InputSystem;
class ResourceSystem;
class ResourceLoader;
class Scene;
class Node;
class Component;
} // namespace engine

namespace engine::plugins::script::bindings
{

using Arguments = vm::Arguments;

class BindingContext final
{
public:
    explicit BindingContext(WorldSystem* world, RenderSystem* renderer = nullptr,
                            UiSystem* ui = nullptr, PhysicsSystem* physics = nullptr,
                            NavigationSystem* navigation = nullptr,
                            InputSystem* input = nullptr, WindowSystem* window = nullptr) noexcept;
    ~BindingContext() noexcept;
    BindingContext(const BindingContext&) = delete;
    BindingContext& operator=(const BindingContext&) = delete;
    BindingContext(BindingContext&&) = delete;
    BindingContext& operator=(BindingContext&&) = delete;

    void activate() noexcept;
    void begin_call(double delta_time) noexcept;
    void end_call() noexcept;
    void shutdown() noexcept;
    [[nodiscard]] WorldSystem* world() const noexcept;
    [[nodiscard]] RenderSystem* renderer() const noexcept;
    [[nodiscard]] UiSystem* ui() const noexcept;
    [[nodiscard]] PhysicsSystem* physics() const noexcept;
    [[nodiscard]] NavigationSystem* navigation() const noexcept;
    [[nodiscard]] InputSystem* input() const noexcept;
    [[nodiscard]] WindowSystem* window() const noexcept;
    [[nodiscard]] bool set_mouse_mode(MouseMode mode);
    [[nodiscard]] bool begin_scene_load(std::string_view path);
    [[nodiscard]] ResourceLoadStatus step_scene_load(std::uint32_t resource_budget,
                                                    ResourceLoadProgress& output);
    [[nodiscard]] Scene* commit_scene_load();
    void cancel_scene_load() noexcept;
    [[nodiscard]] std::string_view scene_load_error() const noexcept;
    [[nodiscard]] std::uint64_t submit_async(
        const std::function<bool(std::function<void(bool)>)>& start);
    // -2 失效、-1 未完成、0 失败、1 成功；已完成结果领取后删除。
    [[nodiscard]] int take_async(std::uint64_t request);
    void remember_input_channel(schema::InputChannelHandle channel);
    [[nodiscard]] bool owns_input_channel(schema::InputChannelHandle channel) const noexcept;
    void forget_input_channel(schema::InputChannelHandle channel);
    void remember_input_capture(schema::InputChannelHandle channel,
                                schema::InputCaptureHandle capture);
    [[nodiscard]] bool owns_input_capture(schema::InputCaptureHandle capture) const noexcept;
    void set_input_error(std::string_view error);
    [[nodiscard]] const std::string& input_error() const noexcept;
    [[nodiscard]] std::uint64_t scene_id() const noexcept;
    void set_scene_id(std::uint64_t value) noexcept;
    [[nodiscard]] double delta_time() const noexcept;
    [[nodiscard]] KeyValueStore& storage();
    void set_storage_error(std::string_view error);
    [[nodiscard]] const std::string& storage_error();

    // owner 由当前 Run 的虚拟机提供；Run 必须覆盖所有原生回调的生命期。
    [[nodiscard]] static BindingContext* current(const Arguments& arguments,
                                                 bool require_callable = true);
    [[nodiscard]] static Scene* scene(const Arguments& arguments);
    [[nodiscard]] static Node* node(const Arguments& arguments);
    [[nodiscard]] static Component* component(const Arguments& arguments);
    [[nodiscard]] static bool identifier(const Arguments& arguments, int index,
                                         std::uint64_t& output);
    [[nodiscard]] static bool number(const Arguments& arguments, int index, double& output);
    [[nodiscard]] static bool scalar(const Arguments& arguments, int index, float& output);
    [[nodiscard]] static bool text(const Arguments& arguments, int index, std::string& output);

private:
    [[nodiscard]] bool scene_load_context_valid() const noexcept;
    void fail_scene_load(std::string_view error);

    struct AsyncResult
    {
        bool ready = false;
        bool success = false;
    };
    std::map<std::uint64_t, std::shared_ptr<AsyncResult>> async_results_;
    std::uint64_t async_sequence_ = 0;
    WorldSystem* world_ = nullptr;
    RenderSystem* renderer_ = nullptr;
    UiSystem* ui_ = nullptr;
    // 可空借用，生命周期覆盖全部玩法回调。
    PhysicsSystem* physics_ = nullptr;
    NavigationSystem* navigation_ = nullptr;
    InputSystem* input_ = nullptr;
    WindowSystem* window_ = nullptr;
    MouseMode previous_mouse_mode_ = MouseMode::normal;
    bool restore_mouse_mode_ = false;
    std::unique_ptr<ResourceLoad> scene_load_;
    ResourceSystem* scene_load_resources_ = nullptr;
    ResourceLoader* scene_load_loader_ = nullptr;
    ResourceLoadProgress scene_load_progress_;
    ResourceLoadStatus scene_load_status_ = ResourceLoadStatus::loading;
    std::uint64_t scene_load_generation_ = 0;
    std::uint64_t scene_restore_request_ = 0;
    std::string scene_load_path_;
    std::string scene_load_error_;
    std::vector<schema::InputChannelHandle> input_channels_;
    std::vector<std::pair<schema::InputChannelHandle, schema::InputCaptureHandle>> input_captures_;
    std::string input_error_;
    bool active_ = false;
    std::uint64_t scene_id_ = 0;
    bool callable_ = false;
    double delta_time_ = 0.0;
    KeyValueStore storage_;
    std::string storage_error_;
    bool storage_attempted_ = false;
};

} // namespace engine::plugins::script::bindings
