#include "plugins/runtime/script/bindings/binding_context.hpp"

#include "runtime/scene/scene.hpp"
#include "runtime/world/world_system.hpp"

#include <cmath>
#include <limits>

namespace engine::plugins::script::bindings
{
namespace
{
constexpr std::size_t maximum_text_size = 4 * 1024 * 1024;
constexpr auto attributes = static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontDelete);

v8::MaybeLocal<v8::String> make_string(v8::Isolate* isolate, std::string_view value)
{
    if (value.size() > maximum_text_size)
    {
        return {};
    }
    return v8::String::NewFromUtf8(isolate, value.data(), v8::NewStringType::kNormal,
                                   static_cast<int>(value.size()));
}
} // namespace

BindingContext::BindingContext(WorldSystem* world, RenderSystem* renderer) noexcept
    : world_(world), renderer_(renderer)
{
}

BindingContext::~BindingContext() noexcept
{
    shutdown();
}

bool BindingContext::initialize(v8::Local<v8::Context> context)
{
    if (!context_.IsEmpty() || context.IsEmpty() || v8::Isolate::GetCurrent() == nullptr)
    {
        return false;
    }
    isolate_ = v8::Isolate::GetCurrent();
    context_.Reset(isolate_, context);
    context->SetAlignedPointerInEmbedderData(1, this, v8::kEmbedderDataTypeTagDefault);
    return true;
}

void BindingContext::activate()
{
    if (!context_.IsEmpty())
    {
        isolate_->SetData(1, this);
    }
}

void BindingContext::begin_call(double delta_time) noexcept
{
    callable_ = true;
    delta_time_ = std::isfinite(delta_time) ? delta_time : 0.0;
}

void BindingContext::end_call() noexcept
{
    callable_ = false;
    delta_time_ = 0.0;
}

void BindingContext::shutdown() noexcept
{
    end_call();
    if (world_ != nullptr && world_->scene() != nullptr && world_->scene()->id() == scene_id_)
    {
        world_->destroy_scene();
    }
    scene_id_ = 0;
    if (!context_.IsEmpty())
    {
        v8::Isolate::Scope isolate_scope(isolate_);
        v8::HandleScope handles(isolate_);
        const auto context = context_.Get(isolate_);
        if (isolate_->GetData(1) == this)
        {
            isolate_->SetData(1, nullptr);
        }
        context->SetAlignedPointerInEmbedderData(1, nullptr, v8::kEmbedderDataTypeTagDefault);
        context_.Reset();
        isolate_ = nullptr;
    }
}

WorldSystem* BindingContext::world() const noexcept
{
    return world_;
}

RenderSystem* BindingContext::renderer() const noexcept
{
    return callable_ ? renderer_ : nullptr;
}

std::uint64_t BindingContext::scene_id() const noexcept
{
    return scene_id_;
}

void BindingContext::set_scene_id(std::uint64_t value) noexcept
{
    scene_id_ = value;
}

double BindingContext::delta_time() const noexcept
{
    return callable_ ? delta_time_ : 0.0;
}

BindingContext* BindingContext::current(const Arguments& arguments, bool require_callable)
{
    auto* isolate = arguments.GetIsolate();
    const auto context = isolate->GetCurrentContext();
    auto* active = static_cast<BindingContext*>(isolate->GetData(1));
    if (active == nullptr || context.IsEmpty() || context->GetNumberOfEmbedderDataFields() < 2 ||
        context->GetAlignedPointerFromEmbedderData(1, v8::kEmbedderDataTypeTagDefault) != active)
    {
        return nullptr;
    }
    if (active->context_.IsEmpty() || active->context_.Get(isolate) != context ||
        (require_callable && !active->callable_))
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
    if (value == nullptr || arguments.Length() < 3 || !arguments[2]->IsInt32())
    {
        return nullptr;
    }
    const auto kind = arguments[2].As<v8::Int32>()->Value();
    if (kind <= 0 || kind > std::numeric_limits<std::uint8_t>::max())
    {
        return nullptr;
    }
    return value->get_component(static_cast<ComponentType>(kind));
}

bool BindingContext::identifier(const Arguments& arguments, int index, std::uint64_t& output)
{
    output = 0;
    if (index < 0 || index >= arguments.Length() || !arguments[index]->IsBigInt())
    {
        return false;
    }
    bool lossless = false;
    const auto value = arguments[index].As<v8::BigInt>()->Uint64Value(&lossless);
    if (lossless)
    {
        output = value;
    }
    return lossless;
}

bool BindingContext::number(const Arguments& arguments, int index, double& output)
{
    output = 0.0;
    if (index < 0 || index >= arguments.Length() || !arguments[index]->IsNumber())
    {
        return false;
    }
    const auto value = arguments[index].As<v8::Number>()->Value();
    if (!std::isfinite(value))
    {
        return false;
    }
    output = value;
    return true;
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
    if (index < 0 || index >= arguments.Length() || !arguments[index]->IsString() ||
        arguments[index].As<v8::String>()->Length() > static_cast<int>(maximum_text_size))
    {
        return false;
    }
    const v8::String::Utf8Value value(arguments.GetIsolate(), arguments[index]);
    if (*value == nullptr || static_cast<std::size_t>(value.length()) > maximum_text_size)
    {
        return false;
    }
    output.assign(*value, static_cast<std::size_t>(value.length()));
    return true;
}

bool BindingContext::define(v8::Local<v8::Context> context, v8::Local<v8::Object> native,
                            std::string_view name, v8::Local<v8::Value> value)
{
    v8::Local<v8::String> key;
    return make_string(v8::Isolate::GetCurrent(), name).ToLocal(&key) &&
           !native->HasOwnProperty(context, key).FromMaybe(true) &&
           native->DefineOwnProperty(context, key, value, attributes).FromMaybe(false);
}

bool BindingContext::function(v8::Local<v8::Context> context, v8::Local<v8::Object> native,
                              std::string_view name, v8::FunctionCallback callback,
                              v8::Local<v8::Value> data)
{
    v8::Local<v8::Function> value;
    return v8::Function::New(context, callback, data).ToLocal(&value) &&
           define(context, native, name, value);
}

bool BindingContext::integer(v8::Local<v8::Context> context, v8::Local<v8::Object> native,
                             std::string_view name, std::int32_t value)
{
    return define(context, native, name, v8::Int32::New(v8::Isolate::GetCurrent(), value));
}

bool BindingContext::string(v8::Local<v8::Context> context, v8::Local<v8::Object> native,
                            std::string_view name, std::string_view value)
{
    v8::Local<v8::String> text;
    return make_string(v8::Isolate::GetCurrent(), value).ToLocal(&text) &&
           define(context, native, name, text);
}

} // namespace engine::plugins::script::bindings
