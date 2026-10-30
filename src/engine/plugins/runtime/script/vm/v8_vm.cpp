#include "plugins/runtime/script/vm/script_vm.hpp"

#include "runtime/app/app.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/utils/path_utils.hpp"

#include <SDL3/SDL.h>
#include <libplatform/libplatform.h>
#include <v8.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <system_error>

#if !ENGINE_SCRIPT_BACKEND_V8
#error "The V8 implementation requires ENGINE_SCRIPT_BACKEND_V8"
#endif

namespace engine::plugins::script::vm
{
namespace
{
namespace fs = std::filesystem;

#if defined(SDL_PLATFORM_ANDROID) && ENGINE_V8_HAS_I18N
[[nodiscard]] bool contained_file(const fs::path& root, std::string_view relative, fs::path& path,
                                  bool directory = false)
{
    std::error_code error;
    const auto canonical_root = fs::canonical(root, error);
    if (error || !fs::is_directory(canonical_root, error) || error)
    {
        return false;
    }
    const auto candidate = fs::canonical(canonical_root / PathUtils::from_utf8(relative), error);
    if (error)
    {
        return false;
    }
    auto component = candidate.begin();
    for (const auto& root_component : canonical_root)
    {
        if (component == candidate.end() || *component != root_component)
        {
            return false;
        }
        ++component;
    }
    const bool correct_type =
        directory ? fs::is_directory(candidate, error) : fs::is_regular_file(candidate, error);
    if (component == candidate.end() || !correct_type || error)
    {
        return false;
    }
    path = candidate;
    return true;
}

#endif

#if defined(SDL_PLATFORM_ANDROID) && ENGINE_V8_HAS_I18N
[[nodiscard]] bool prepare_directory(const fs::path& root, std::string_view relative,
                                     fs::path& directory)
{
    std::error_code error;
    fs::create_directories(root / PathUtils::from_utf8(relative), error);
    return !error && contained_file(root, relative, directory, true);
}

[[nodiscard]] bool cache_file(const fs::path& directory, std::string_view name,
                              std::string_view bytes)
{
    const fs::path target = directory / PathUtils::from_utf8(name);
    std::error_code error;
    const auto status = fs::symlink_status(target, error);
    std::string previous;
    if (!error && fs::is_regular_file(status) && ResourceSystem::read_text(target, previous) &&
        previous == bytes)
    {
        return true;
    }

    // 临时文件位于已校验目录；先移除残留链接，发布时替换目标本身而不跟随目标链接。
    const fs::path temporary = directory / PathUtils::from_utf8(std::string(name) + ".tmp");
    fs::remove(temporary, error);
    if (error)
    {
        return false;
    }
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    file.close();
    if (!file ||
        !SDL_RenamePath(PathUtils::to_utf8(temporary).c_str(), PathUtils::to_utf8(target).c_str()))
    {
        fs::remove(temporary, error);
        return false;
    }
    return true;
}

#endif

#if ENGINE_V8_HAS_I18N
[[nodiscard]] bool prepare_icu_path(std::string& path)
{
#if defined(SDL_PLATFORM_ANDROID)
    if (!App::is_initialized())
    {
        return false;
    }
    std::vector<std::byte> bytes;
    fs::path directory;
    if (!ResourceSystem::read_binary(PathUtils::from_utf8("apk:/assets/v8/icudtl.dat"), bytes) ||
        bytes.empty() || !prepare_directory(App::cache_path(), "v8", directory) ||
        !cache_file(directory, "icudtl.dat",
                    std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size())))
    {
        return false;
    }
    path = PathUtils::to_utf8(directory / "icudtl.dat");
#else
    const char* base = SDL_GetBasePath();
    if (base == nullptr)
    {
        return false;
    }
    path = PathUtils::to_utf8(PathUtils::from_utf8(base) / "icudtl.dat");
#endif
    return true;
}
#endif

[[nodiscard]] v8::MaybeLocal<v8::String> js_string(v8::Isolate* isolate, std::string_view text)
{
    if (text.size() > maximum_text_size)
    {
        return {};
    }
    return v8::String::NewFromUtf8(isolate, text.data(), v8::NewStringType::kNormal,
                                   static_cast<int>(text.size()));
}

std::string value_text(v8::Isolate* isolate, v8::Local<v8::Value> value)
{
    const v8::String::Utf8Value text(isolate, value);
    return *text == nullptr ? std::string{}
                            : std::string(*text, static_cast<std::size_t>(text.length()));
}

struct V8Process
{
    std::unique_ptr<v8::Platform> platform;
    bool ready = false;
    bool attempted = false;

    ~V8Process()
    {
        if (ready)
        {
            (void)v8::V8::Dispose();
            v8::V8::DisposePlatform();
        }
    }

    [[nodiscard]] bool initialize()
    {
        if (ready || attempted)
        {
            return ready;
        }
#if ENGINE_V8_HAS_I18N
        std::string icu;
        if (!prepare_icu_path(icu))
        {
            return false;
        }
        if (!v8::V8::InitializeICUDefaultLocation(nullptr, icu.c_str()))
        {
            return false;
        }
#endif
        attempted = true;
#if ENGINE_V8_HAS_TEMPORAL
        v8::V8::SetFlagsFromString("--harmony-temporal");
#endif
        platform = v8::platform::NewDefaultPlatform();
        if (!platform)
        {
            return false;
        }
        v8::V8::InitializePlatform(platform.get());
        ready = v8::V8::Initialize();
        if (!ready)
        {
            v8::V8::DisposePlatform();
            platform.reset();
        }
        return ready;
    }
};

std::shared_ptr<V8Process> v8_process()
{
    // 静态引用保留平台到进程退出；isolate 的引用覆盖静态析构顺序，不能先释放平台。
    static const auto process = std::make_shared<V8Process>();
    return process;
}

[[nodiscard]] v8::MaybeLocal<v8::Value> native_value(v8::Isolate* isolate, const NativeValue& value,
                                                     std::size_t depth = 0)
{
    if (depth > 64)
    {
        return {};
    }
    const auto context = isolate->GetCurrentContext();
    if (std::holds_alternative<std::monostate>(value.data))
    {
        return v8::Null(isolate);
    }
    if (const auto* boolean = std::get_if<bool>(&value.data))
    {
        return v8::Boolean::New(isolate, *boolean);
    }
    if (const auto* number = std::get_if<double>(&value.data))
    {
        return v8::Number::New(isolate, *number);
    }
    if (const auto* identifier = std::get_if<std::uint64_t>(&value.data))
    {
        return v8::BigInt::NewFromUnsigned(isolate, *identifier);
    }
    if (const auto* text = std::get_if<std::string>(&value.data))
    {
        v8::Local<v8::String> result;
        return js_string(isolate, *text).ToLocal(&result) ? v8::MaybeLocal<v8::Value>(result)
                                                          : v8::MaybeLocal<v8::Value>();
    }
    if (const auto* items = std::get_if<NativeValue::Array>(&value.data))
    {
        if (items->size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        {
            return {};
        }
        const auto result = v8::Array::New(isolate, static_cast<int>(items->size()));
        for (std::size_t index = 0; index < items->size(); ++index)
        {
            v8::Local<v8::Value> item;
            if (!native_value(isolate, (*items)[index], depth + 1).ToLocal(&item) ||
                !result->CreateDataProperty(context, static_cast<std::uint32_t>(index), item)
                     .FromMaybe(false))
            {
                return {};
            }
        }
        return result;
    }
    if (const auto* properties = std::get_if<NativeValue::Object>(&value.data))
    {
        const auto result = v8::Object::New(isolate);
        for (const auto& [name, property] : *properties)
        {
            v8::Local<v8::String> key;
            v8::Local<v8::Value> item;
            if (!js_string(isolate, name).ToLocal(&key) ||
                !native_value(isolate, property, depth + 1).ToLocal(&item) ||
                !result
                     ->DefineOwnProperty(
                         context, key, item,
                         static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontDelete))
                     .FromMaybe(false))
            {
                return {};
            }
        }
        return result;
    }
    return {};
}

class V8Arguments final : public Arguments
{
public:
    V8Arguments(const v8::FunctionCallbackInfo<v8::Value>& arguments, void* owner,
                std::int32_t data) noexcept
        : arguments_(arguments), owner_(owner), data_(data)
    {
    }

    int count() const noexcept override
    {
        return arguments_.Length();
    }

    void* owner() const noexcept override
    {
        return owner_;
    }

    std::int32_t data() const noexcept override
    {
        return data_;
    }

    bool is_boolean(int index) const override
    {
        return valid_index(index) && arguments_[index]->IsBoolean();
    }

    bool boolean(int index) const override
    {
        return is_boolean(index) && arguments_[index]->IsTrue();
    }

    bool is_int32(int index) const override
    {
        return valid_index(index) && arguments_[index]->IsInt32();
    }

    std::int32_t integer(int index) const override
    {
        return is_int32(index) ? arguments_[index].As<v8::Int32>()->Value() : 0;
    }

    bool is_uint32(int index) const override
    {
        return valid_index(index) && arguments_[index]->IsUint32();
    }

    std::uint32_t unsigned_integer(int index) const override
    {
        return is_uint32(index) ? arguments_[index].As<v8::Uint32>()->Value() : 0;
    }

    bool identifier(int index, std::uint64_t& output) const override
    {
        output = 0;
        if (!valid_index(index) || !arguments_[index]->IsBigInt())
        {
            return false;
        }
        bool lossless = false;
        const auto value = arguments_[index].As<v8::BigInt>()->Uint64Value(&lossless);
        if (lossless)
        {
            output = value;
        }
        return lossless;
    }

    bool number(int index, double& output) const override
    {
        output = 0.0;
        if (!valid_index(index) || !arguments_[index]->IsNumber())
        {
            return false;
        }
        const auto value = arguments_[index].As<v8::Number>()->Value();
        if (!std::isfinite(value))
        {
            return false;
        }
        output = value;
        return true;
    }

    bool text(int index, std::string& output) const override
    {
        output.clear();
        if (!valid_index(index) || !arguments_[index]->IsString() ||
            arguments_[index].As<v8::String>()->Length() > static_cast<int>(maximum_text_size))
        {
            return false;
        }
        const v8::String::Utf8Value value(arguments_.GetIsolate(), arguments_[index]);
        if (*value == nullptr || static_cast<std::size_t>(value.length()) > maximum_text_size)
        {
            return false;
        }
        output.assign(*value, static_cast<std::size_t>(value.length()));
        return true;
    }

    void set_return(const NativeValue& value) const override
    {
        v8::Local<v8::Value> result;
        if (!native_value(arguments_.GetIsolate(), value).ToLocal(&result))
        {
            type_error("Cannot convert native return data");
            return;
        }
        arguments_.GetReturnValue().Set(result);
    }

    void type_error(std::string_view message) const override
    {
        auto* isolate = arguments_.GetIsolate();
        v8::Local<v8::String> text;
        if (js_string(isolate, message).ToLocal(&text))
        {
            isolate->ThrowException(v8::Exception::TypeError(text));
        }
    }

private:
    [[nodiscard]] bool valid_index(int index) const noexcept
    {
        return index >= 0 && index < arguments_.Length();
    }

    const v8::FunctionCallbackInfo<v8::Value>& arguments_;
    void* owner_ = nullptr;
    std::int32_t data_ = 0;
};

class V8Vm final : public ScriptVm
{
public:
    V8Vm() = default;
    V8Vm(const V8Vm&) = delete;
    V8Vm& operator=(const V8Vm&) = delete;
    V8Vm(V8Vm&&) = delete;
    V8Vm& operator=(V8Vm&&) = delete;

    ~V8Vm() noexcept override
    {
        if (isolate_ == nullptr)
        {
            return;
        }
        {
            v8::Isolate::Scope isolate_scope(isolate_);
            v8::HandleScope handles(isolate_);
            isolate_->SetData(0, nullptr);
            gameplay_.Reset();
            rejections_.clear();
            modules_.clear();
            native_.Reset();
            context_.Reset();
        }
        isolate_->Dispose();
        isolate_ = nullptr;
    }

    bool initialize(void* owner) override
    {
        error_.clear();
        if (isolate_ != nullptr)
        {
            return fail("Script VM is already initialized");
        }
        process_ = v8_process();
        if (!process_->initialize())
        {
            return fail("Cannot initialize the V8 platform");
        }
        allocator_.reset(v8::ArrayBuffer::Allocator::NewDefaultAllocator());
        if (allocator_ == nullptr)
        {
            return fail("Cannot create V8 allocator");
        }
        v8::Isolate::CreateParams parameters;
        parameters.array_buffer_allocator = allocator_.get();
        isolate_ = v8::Isolate::New(parameters);
        if (isolate_ == nullptr)
        {
            return fail("Cannot create V8 isolate");
        }
        owner_ = owner;
        isolate_->SetData(0, this);
        isolate_->SetMicrotasksPolicy(v8::MicrotasksPolicy::kExplicit);
        isolate_->SetHostImportModuleDynamicallyCallback(dynamic_import);
        isolate_->SetPromiseRejectCallback(promise_rejection);
        v8::Isolate::Scope isolate_scope(isolate_);
        v8::HandleScope handles(isolate_);
        const auto context = v8::Context::New(isolate_);
        if (context.IsEmpty())
        {
            return fail("Cannot create gameplay context");
        }
        v8::Context::Scope context_scope(context);
        context_.Reset(isolate_, context);
        native_.Reset(isolate_, v8::Object::New(isolate_));
        return true;
    }

    bool function(std::string_view name, NativeCallback callback, std::int32_t data) override
    {
        error_.clear();
        if (!can_register() || callback == nullptr)
        {
            return fail("Native function registration requires an unsealed initialized VM");
        }
        v8::Isolate::Scope isolate_scope(isolate_);
        v8::HandleScope handles(isolate_);
        const auto context = context_.Get(isolate_);
        v8::Context::Scope context_scope(context);
        v8::TryCatch caught(isolate_);
        auto binding = std::make_unique<NativeFunction>();
        binding->callback = callback;
        binding->data = data;
        v8::Local<v8::Function> function;
        if (!v8::Function::New(
                 context, invoke,
                 v8::External::New(isolate_, binding.get(), v8::kExternalPointerTypeTagDefault))
                 .ToLocal(&function) ||
            !define(native_.Get(isolate_), name, function))
        {
            return exception(caught, "Cannot register native function: " + std::string(name));
        }
        functions_.push_back(std::move(binding));
        return true;
    }

    bool integer(std::string_view name, std::int32_t value) override
    {
        error_.clear();
        if (!can_register())
        {
            return fail("Native constant registration requires an unsealed initialized VM");
        }
        v8::Isolate::Scope isolate_scope(isolate_);
        v8::HandleScope handles(isolate_);
        v8::Context::Scope context_scope(context_.Get(isolate_));
        v8::TryCatch caught(isolate_);
        return define(native_.Get(isolate_), name, v8::Int32::New(isolate_, value)) ||
               exception(caught, "Cannot register native constant: " + std::string(name));
    }

    bool string(std::string_view name, std::string_view value) override
    {
        error_.clear();
        if (!can_register())
        {
            return fail("Native string registration requires an unsealed initialized VM");
        }
        v8::Isolate::Scope isolate_scope(isolate_);
        v8::HandleScope handles(isolate_);
        v8::Context::Scope context_scope(context_.Get(isolate_));
        v8::TryCatch caught(isolate_);
        v8::Local<v8::String> text;
        return (js_string(isolate_, value).ToLocal(&text) &&
                define(native_.Get(isolate_), name, text)) ||
               exception(caught, "Cannot register native string: " + std::string(name));
    }

    bool seal_bindings() override
    {
        error_.clear();
        if (!can_register())
        {
            return fail("Native bindings are unavailable or already sealed");
        }
        v8::Isolate::Scope isolate_scope(isolate_);
        v8::HandleScope handles(isolate_);
        const auto context = context_.Get(isolate_);
        v8::Context::Scope context_scope(context);
        v8::TryCatch caught(isolate_);
        const auto native = native_.Get(isolate_);
        if (!native->SetIntegrityLevel(context, v8::IntegrityLevel::kFrozen).FromMaybe(false) ||
            !define(context->Global(), "__engineNative", native))
        {
            return exception(caught, "Cannot publish native bindings");
        }
        sealed_ = true;
        return true;
    }

    bool prepare(std::string_view entry, SourceLoader loader) override
    {
        error_.clear();
        if (!ready() || !sealed_ || !modules_.empty() || !loader || entry.empty())
        {
            return fail("Module preparation requires a fresh VM and source loader");
        }
        loader_ = std::move(loader);
        entry_ = entry;
        v8::Isolate::Scope isolate_scope(isolate_);
        v8::HandleScope handles(isolate_);
        const auto context = context_.Get(isolate_);
        v8::Context::Scope context_scope(context);
        v8::TryCatch caught(isolate_);
        v8::Local<v8::Module> engine;
        v8::Local<v8::Module> module;
        if (!load_module("engine").ToLocal(&engine) || !load_module(entry_).ToLocal(&module) ||
            !module->InstantiateModule(context, resolve).FromMaybe(false))
        {
            return exception(caught, "Gameplay module precheck failed");
        }
        if (module->IsGraphAsync())
        {
            return fail("Top-level await is not supported by gameplay lifecycle");
        }
        prepared_ = true;
        return true;
    }

    bool start() override
    {
        error_.clear();
        if (!ready() || !prepared_ || !gameplay_.IsEmpty())
        {
            return fail("No prepared gameplay is available");
        }
        v8::Isolate::Scope isolate_scope(isolate_);
        v8::HandleScope handles(isolate_);
        const auto context = context_.Get(isolate_);
        v8::Context::Scope context_scope(context);
        v8::TryCatch caught(isolate_);
        const auto module = modules_.find(entry_)->second.Get(isolate_);
        v8::Local<v8::Value> evaluation;
        if (!module->Evaluate(context).ToLocal(&evaluation))
        {
            return exception(caught, "Gameplay module execution failed");
        }
        if (evaluation->IsPromise())
        {
            const auto promise = evaluation.As<v8::Promise>();
            if (promise->State() == v8::Promise::kRejected)
            {
                return fail("Gameplay module execution failed: " +
                            value_text(isolate_, promise->Result()));
            }
            if (promise->State() != v8::Promise::kFulfilled)
            {
                return fail("Gameplay module execution must be synchronous");
            }
        }
        const auto engine = modules_.find("engine")->second.Get(isolate_);
        v8::Local<v8::Value> constructor;
        v8::Local<v8::Value> base;
        v8::Local<v8::Object> object;
        if (engine->GetStatus() != v8::Module::kEvaluated ||
            !module->GetModuleNamespace()
                 .As<v8::Object>()
                 ->Get(context, v8::String::NewFromUtf8Literal(isolate_, "default"))
                 .ToLocal(&constructor) ||
            !constructor->IsFunction() ||
            !constructor.As<v8::Function>()->NewInstance(context).ToLocal(&object) ||
            !engine->GetModuleNamespace()
                 .As<v8::Object>()
                 ->Get(context, v8::String::NewFromUtf8Literal(isolate_, "Gameplay"))
                 .ToLocal(&base) ||
            !base->IsFunction() ||
            !object->InstanceOf(context, base.As<v8::Function>()).FromMaybe(false))
        {
            return exception(caught, "Entry must default-export a class extending Gameplay");
        }
        gameplay_.Reset(isolate_, object);
        return true;
    }

    bool call_gameplay(std::string_view name, bool require_true) override
    {
        error_.clear();
        if (!ready() || gameplay_.IsEmpty())
        {
            return fail("Gameplay instance is unavailable");
        }
        v8::Isolate::Scope isolate_scope(isolate_);
        v8::HandleScope handles(isolate_);
        v8::Context::Scope context_scope(context_.Get(isolate_));
        return call(gameplay_.Get(isolate_), name, require_true, "Gameplay.");
    }

    bool call_binding(std::string_view name) override
    {
        error_.clear();
        if (!ready())
        {
            return fail("Script VM is not initialized");
        }
        v8::Isolate::Scope isolate_scope(isolate_);
        v8::HandleScope handles(isolate_);
        v8::Context::Scope context_scope(context_.Get(isolate_));
        const auto found = modules_.find("engine");
        if (found == modules_.end() ||
            found->second.Get(isolate_)->GetStatus() != v8::Module::kEvaluated)
        {
            return true;
        }
        const auto object = found->second.Get(isolate_)->GetModuleNamespace().As<v8::Object>();
        return call(object, name, false, "Script binding callback ");
    }

    bool drain_jobs() override
    {
        error_.clear();
        if (!ready())
        {
            return fail("Script VM is not initialized");
        }
        v8::Isolate::Scope isolate_scope(isolate_);
        v8::HandleScope handles(isolate_);
        v8::Context::Scope context_scope(context_.Get(isolate_));
        v8::TryCatch caught(isolate_);
        isolate_->PerformMicrotaskCheckpoint();
        if (caught.HasCaught())
        {
            return exception(caught, "Script promise processing failed");
        }
        if (!async_error_.empty())
        {
            return fail(async_error_);
        }
        if (!rejections_.empty())
        {
            return fail(rejections_.front().message);
        }
        return true;
    }

    const std::string& last_error() const noexcept override
    {
        return error_;
    }

    const std::vector<std::string>& loaded_modules() const noexcept override
    {
        return loaded_modules_;
    }

private:
    struct Rejection
    {
        v8::Global<v8::Promise> promise;
        std::string message;
    };

    struct NativeFunction
    {
        NativeCallback callback = nullptr;
        std::int32_t data = 0;
    };

    static void invoke(const v8::FunctionCallbackInfo<v8::Value>& arguments)
    {
        auto* isolate = arguments.GetIsolate();
        auto* vm = static_cast<V8Vm*>(isolate->GetData(0));
        if (vm == nullptr || vm->context_.IsEmpty() ||
            vm->context_.Get(isolate) != isolate->GetCurrentContext() ||
            !arguments.Data()->IsExternal())
        {
            arguments.GetReturnValue().SetNull();
            return;
        }
        const auto* function = static_cast<NativeFunction*>(
            arguments.Data().As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault));
        const V8Arguments values(arguments, vm->owner_, function->data);
        function->callback(values);
    }

    [[nodiscard]] bool ready() const noexcept
    {
        return isolate_ != nullptr && !context_.IsEmpty();
    }

    [[nodiscard]] bool can_register() const noexcept
    {
        return ready() && !sealed_;
    }

    bool fail(std::string message)
    {
        error_ = std::move(message);
        return false;
    }

    bool exception(v8::TryCatch& caught, std::string message)
    {
        if (caught.HasCaught())
        {
            message += ": " + value_text(isolate_, caught.Exception());
            const auto location = caught.Message();
            if (!location.IsEmpty())
            {
                message +=
                    " (" + value_text(isolate_, location->GetScriptResourceName()) + ":" +
                    std::to_string(
                        location->GetLineNumber(isolate_->GetCurrentContext()).FromMaybe(0)) +
                    ")";
            }
        }
        return fail(std::move(message));
    }

    void module_error(std::string_view message)
    {
        v8::Local<v8::String> text;
        if (js_string(isolate_, message).ToLocal(&text))
        {
            isolate_->ThrowException(v8::Exception::Error(text));
        }
    }

    [[nodiscard]] bool define(v8::Local<v8::Object> object, std::string_view name,
                              v8::Local<v8::Value> value)
    {
        const auto context = context_.Get(isolate_);
        v8::Local<v8::String> key;
        constexpr auto attributes =
            static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontDelete);
        return js_string(isolate_, name).ToLocal(&key) &&
               !object->HasOwnProperty(context, key).FromMaybe(true) &&
               object->DefineOwnProperty(context, key, value, attributes).FromMaybe(false);
    }

    [[nodiscard]] v8::MaybeLocal<v8::Module> load_module(const std::string& path)
    {
        if (const auto found = modules_.find(path); found != modules_.end())
        {
            return found->second.Get(isolate_);
        }
        if (modules_.size() >= maximum_module_count)
        {
            module_error("Module graph exceeds 256 files");
            return {};
        }
        std::string source;
        if (!loader_(path, source) || source.size() > maximum_text_size)
        {
            module_error("Cannot read module from supplied catalog: " + path);
            return {};
        }
        v8::Local<v8::String> text;
        v8::Local<v8::String> name;
        if (!js_string(isolate_, source).ToLocal(&text) ||
            !js_string(isolate_, path).ToLocal(&name))
        {
            return {};
        }
        const v8::ScriptOrigin origin(name, 0, 0, false, -1, {}, false, false, true);
        v8::ScriptCompiler::Source input(text, origin);
        v8::Local<v8::Module> module;
        if (!v8::ScriptCompiler::CompileModule(isolate_, &input).ToLocal(&module))
        {
            return {};
        }
        modules_.emplace(path, v8::Global<v8::Module>(isolate_, module));
        loaded_modules_.push_back(path);
        return module;
    }

    static v8::MaybeLocal<v8::Module> resolve(v8::Local<v8::Context> context,
                                              v8::Local<v8::String> specifier,
                                              v8::Local<v8::FixedArray> attributes,
                                              v8::Local<v8::Module> referrer)
    {
        auto* isolate = v8::Isolate::GetCurrent();
        auto* vm = static_cast<V8Vm*>(isolate->GetData(0));
        if (vm == nullptr || vm->context_.Get(isolate) != context)
        {
            return {};
        }
        if (attributes->Length() != 0)
        {
            vm->module_error("Import attributes are not supported");
            return {};
        }
        const auto request = value_text(isolate, specifier);
        for (const auto& [name, module] : vm->modules_)
        {
            if (module.Get(isolate) == referrer)
            {
                std::string path;
                if (!resolve_module_path(name, request, path))
                {
                    vm->module_error("Unsupported module import: " + request);
                    return {};
                }
                return vm->load_module(path);
            }
        }
        vm->module_error("Unknown module referrer");
        return {};
    }

    [[nodiscard]] bool call(v8::Local<v8::Object> object, std::string_view name, bool require_true,
                            std::string_view prefix)
    {
        const auto context = context_.Get(isolate_);
        v8::TryCatch caught(isolate_);
        v8::Local<v8::String> key;
        v8::Local<v8::Value> member;
        v8::Local<v8::Value> result;
        const std::string operation = std::string(prefix) + std::string(name);
        if (!js_string(isolate_, name).ToLocal(&key) ||
            !object->Get(context, key).ToLocal(&member) || !member->IsFunction() ||
            !member.As<v8::Function>()->Call(context, object, 0, nullptr).ToLocal(&result))
        {
            return exception(caught, operation + " failed");
        }
        if (result->IsPromise())
        {
            return fail(operation + " must be synchronous");
        }
        if (require_true && !result->IsTrue())
        {
            return fail(operation + " must return true to start");
        }
        return true;
    }

    static v8::MaybeLocal<v8::Promise> dynamic_import(v8::Local<v8::Context> context,
                                                      v8::Local<v8::Data>, v8::Local<v8::Value>,
                                                      v8::Local<v8::String>,
                                                      v8::Local<v8::FixedArray>)
    {
        auto* isolate = v8::Isolate::GetCurrent();
        auto* vm = static_cast<V8Vm*>(isolate->GetData(0));
        if (vm != nullptr)
        {
            vm->async_error_ = "Dynamic import is not supported";
        }
        v8::Local<v8::Promise::Resolver> resolver;
        if (!v8::Promise::Resolver::New(context).ToLocal(&resolver) ||
            !resolver
                 ->Reject(context, v8::Exception::Error(v8::String::NewFromUtf8Literal(
                                       isolate, "Dynamic import is not supported")))
                 .FromMaybe(false))
        {
            return {};
        }
        return resolver->GetPromise();
    }

    static void promise_rejection(v8::PromiseRejectMessage message)
    {
        auto* isolate = v8::Isolate::GetCurrent();
        auto* vm = static_cast<V8Vm*>(isolate->GetData(0));
        if (vm == nullptr)
        {
            return;
        }
        const auto found =
            std::find_if(vm->rejections_.begin(), vm->rejections_.end(), [&](const auto& rejection)
                         { return rejection.promise.Get(isolate) == message.GetPromise(); });
        if (message.GetEvent() == v8::kPromiseHandlerAddedAfterReject)
        {
            if (found != vm->rejections_.end())
            {
                vm->rejections_.erase(found);
            }
        }
        else if (message.GetEvent() == v8::kPromiseRejectWithNoHandler &&
                 found == vm->rejections_.end())
        {
            vm->rejections_.push_back(
                {v8::Global<v8::Promise>(isolate, message.GetPromise()),
                 "Unhandled script promise: " + value_text(isolate, message.GetValue())});
        }
    }

    std::shared_ptr<V8Process> process_;
    std::unique_ptr<v8::ArrayBuffer::Allocator> allocator_;
    v8::Isolate* isolate_ = nullptr;
    v8::Global<v8::Context> context_;
    v8::Global<v8::Object> native_;
    v8::Global<v8::Object> gameplay_;
    std::map<std::string, v8::Global<v8::Module>, std::less<>> modules_;
    std::vector<std::string> loaded_modules_;
    std::vector<std::unique_ptr<NativeFunction>> functions_;
    std::vector<Rejection> rejections_;
    SourceLoader loader_;
    void* owner_ = nullptr;
    std::string entry_;
    std::string error_;
    std::string async_error_;
    bool sealed_ = false;
    bool prepared_ = false;
};

} // namespace

std::unique_ptr<ScriptVm> create_script_vm()
{
    return std::make_unique<V8Vm>();
}

} // namespace engine::plugins::script::vm
