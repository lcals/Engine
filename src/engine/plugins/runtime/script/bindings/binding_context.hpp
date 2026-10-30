#pragma once

#include "plugins/runtime/script/vm/script_vm.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace engine
{
class WorldSystem;
class RenderSystem;
class UiSystem;
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
                            UiSystem* ui = nullptr) noexcept;
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
    [[nodiscard]] std::uint64_t scene_id() const noexcept;
    void set_scene_id(std::uint64_t value) noexcept;
    [[nodiscard]] double delta_time() const noexcept;

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
    WorldSystem* world_ = nullptr;
    RenderSystem* renderer_ = nullptr;
    UiSystem* ui_ = nullptr;
    bool active_ = false;
    std::uint64_t scene_id_ = 0;
    bool callable_ = false;
    double delta_time_ = 0.0;
};

} // namespace engine::plugins::script::bindings
