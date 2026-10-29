#pragma once

#include <v8.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace engine
{
class WorldSystem;
class Scene;
class Node;
class Component;
} // namespace engine

namespace engine::script_bindings
{

using Arguments = v8::FunctionCallbackInfo<v8::Value>;

class BindingContext final
{
public:
    explicit BindingContext(WorldSystem* world) noexcept;
    ~BindingContext() noexcept;
    BindingContext(const BindingContext&) = delete;
    BindingContext& operator=(const BindingContext&) = delete;
    BindingContext(BindingContext&&) = delete;
    BindingContext& operator=(BindingContext&&) = delete;

    [[nodiscard]] bool initialize(v8::Local<v8::Context> context);
    void activate();
    void begin_call(double delta_time) noexcept;
    void end_call() noexcept;
    void shutdown() noexcept;
    [[nodiscard]] WorldSystem* world() const noexcept;
    [[nodiscard]] std::uint64_t scene_id() const noexcept;
    void set_scene_id(std::uint64_t value) noexcept;
    [[nodiscard]] double delta_time() const noexcept;

    // 先比较活动地址和实际Context，再解引用。旧Context不持有可调用的Run指针。
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
    [[nodiscard]] static bool define(v8::Local<v8::Context> context, v8::Local<v8::Object> native,
                                     std::string_view name, v8::Local<v8::Value> value);
    [[nodiscard]] static bool function(v8::Local<v8::Context> context, v8::Local<v8::Object> native,
                                       std::string_view name, v8::FunctionCallback callback,
                                       v8::Local<v8::Value> data = {});
    [[nodiscard]] static bool integer(v8::Local<v8::Context> context, v8::Local<v8::Object> native,
                                      std::string_view name, std::int32_t value);
    [[nodiscard]] static bool string(v8::Local<v8::Context> context, v8::Local<v8::Object> native,
                                     std::string_view name, std::string_view value);

private:
    WorldSystem* world_ = nullptr;
    v8::Global<v8::Context> context_;
    v8::Isolate* isolate_ = nullptr;
    std::uint64_t scene_id_ = 0;
    bool callable_ = false;
    double delta_time_ = 0.0;
};

} // namespace engine::script_bindings
