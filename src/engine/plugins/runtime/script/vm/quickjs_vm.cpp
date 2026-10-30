#include "plugins/runtime/script/vm/script_vm.hpp"

#include <quickjs.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <type_traits>

#if !defined(ENGINE_SCRIPT_BACKEND_QUICKJS) || defined(ENGINE_SCRIPT_BACKEND_V8)
#error QuickJS implementation requires the exclusive QuickJS backend
#endif

namespace engine::plugins::script::vm
{
namespace
{
// 本地拥有的 JSValue 与所在 Context 配对，不能逃出虚拟机生命周期。
class Value final
{
public:
    Value(JSContext* context, JSValue value) noexcept : context_(context), value_(value)
    {
    }
    ~Value() noexcept
    {
        JS_FreeValue(context_, value_);
    }
    Value(const Value&) = delete;
    Value& operator=(const Value&) = delete;
    [[nodiscard]] JSValueConst get() const noexcept
    {
        return value_;
    }
    [[nodiscard]] JSValue release() noexcept
    {
        return std::exchange(value_, JS_UNDEFINED);
    }

private:
    JSContext* context_;
    JSValue value_;
};

std::string value_text(JSContext* context, JSValueConst value)
{
    std::size_t length = 0;
    const char* text = JS_ToCStringLen(context, &length, value);
    if (text == nullptr)
    {
        return {};
    }
    std::string result(text, length);
    JS_FreeCString(context, text);
    return result;
}

JSValue native_value(JSContext* context, const NativeValue& input)
{
    return std::visit(
        [context](const auto& value) -> JSValue
        {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, std::monostate>)
            {
                return JS_NULL;
            }
            else if constexpr (std::is_same_v<T, bool>)
            {
                return JS_NewBool(context, value);
            }
            else if constexpr (std::is_same_v<T, double>)
            {
                return JS_NewFloat64(context, value);
            }
            else if constexpr (std::is_same_v<T, std::uint64_t>)
            {
                return JS_NewBigUint64(context, value);
            }
            else if constexpr (std::is_same_v<T, std::string>)
            {
                if (value.size() > maximum_text_size)
                {
                    return JS_ThrowRangeError(context, "Native string exceeds the size limit");
                }
                return JS_NewStringLen(context, value.data(), value.size());
            }
            else if constexpr (std::is_same_v<T, NativeValue::Array>)
            {
                Value result(context, JS_NewArray(context));
                if (JS_IsException(result.get()))
                {
                    return JS_EXCEPTION;
                }
                std::uint32_t index = 0;
                for (const auto& item : value)
                {
                    Value member(context, native_value(context, item));
                    if (JS_IsException(member.get()) ||
                        JS_SetPropertyUint32(context, result.get(), index++, member.release()) < 0)
                    {
                        return JS_EXCEPTION;
                    }
                }
                return result.release();
            }
            else
            {
                Value result(context, JS_NewObject(context));
                if (JS_IsException(result.get()))
                {
                    return JS_EXCEPTION;
                }
                for (const auto& [name, item] : value)
                {
                    Value member(context, native_value(context, item));
                    if (JS_IsException(member.get()) ||
                        JS_DefinePropertyValueStr(context, result.get(), name.c_str(),
                                                  member.release(), JS_PROP_ENUMERABLE) < 0)
                    {
                        return JS_EXCEPTION;
                    }
                }
                return result.release();
            }
        },
        input.data);
}

class QuickJsArguments final : public Arguments
{
public:
    QuickJsArguments(JSContext* context, int count, JSValueConst* values, void* owner,
                     std::int32_t data) noexcept
        : context_(context), count_(count), values_(values), owner_(owner), data_(data)
    {
    }
    QuickJsArguments(const QuickJsArguments&) = delete;
    QuickJsArguments& operator=(const QuickJsArguments&) = delete;
    ~QuickJsArguments() noexcept override
    {
        JS_FreeValue(context_, result_);
    }
    [[nodiscard]] int count() const noexcept override
    {
        return count_;
    }
    [[nodiscard]] void* owner() const noexcept override
    {
        return owner_;
    }
    [[nodiscard]] std::int32_t data() const noexcept override
    {
        return data_;
    }
    [[nodiscard]] bool is_boolean(int index) const override
    {
        return in_range(index) && JS_IsBool(values_[index]);
    }
    [[nodiscard]] bool boolean(int index) const override
    {
        return is_boolean(index) && JS_ToBool(context_, values_[index]) == 1;
    }
    [[nodiscard]] bool is_int32(int index) const override
    {
        double value = 0.0;
        return number(index, value) && value == std::trunc(value) &&
               value >= std::numeric_limits<std::int32_t>::min() &&
               value <= std::numeric_limits<std::int32_t>::max();
    }
    [[nodiscard]] std::int32_t integer(int index) const override
    {
        std::int32_t value = 0;
        if (is_int32(index))
        {
            (void)JS_ToInt32(context_, &value, values_[index]);
        }
        return value;
    }
    [[nodiscard]] bool is_uint32(int index) const override
    {
        double value = 0.0;
        return number(index, value) && value == std::trunc(value) && value >= 0 &&
               value <= std::numeric_limits<std::uint32_t>::max();
    }
    [[nodiscard]] std::uint32_t unsigned_integer(int index) const override
    {
        std::uint32_t value = 0;
        if (is_uint32(index))
        {
            (void)JS_ToUint32(context_, &value, values_[index]);
        }
        return value;
    }
    [[nodiscard]] bool identifier(int index, std::uint64_t& output) const override
    {
        output = 0;
        if (!in_range(index) || !JS_IsBigInt(context_, values_[index]))
        {
            return false;
        }
        // JS_ToBigUint64 按模截断；先检查十进制表示，拒绝负数和溢出身份。
        const auto text = value_text(context_, values_[index]);
        std::uint64_t value = 0;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        {
            return false;
        }
        output = value;
        return true;
    }
    [[nodiscard]] bool number(int index, double& output) const override
    {
        output = 0.0;
        double value = 0.0;
        if (!in_range(index) || !JS_IsNumber(values_[index]) ||
            JS_ToFloat64(context_, &value, values_[index]) < 0 || !std::isfinite(value))
        {
            return false;
        }
        output = value;
        return true;
    }
    [[nodiscard]] bool text(int index, std::string& output) const override
    {
        output.clear();
        if (!in_range(index) || !JS_IsString(values_[index]))
        {
            return false;
        }
        std::size_t length = 0;
        const char* value = JS_ToCStringLen(context_, &length, values_[index]);
        if (value == nullptr)
        {
            return false;
        }
        const bool valid = length <= maximum_text_size;
        if (valid)
        {
            output.assign(value, length);
        }
        JS_FreeCString(context_, value);
        return valid;
    }
    void set_return(const NativeValue& value) const override
    {
        JS_FreeValue(context_, result_);
        result_ = native_value(context_, value);
    }
    void type_error(std::string_view message) const override
    {
        JS_FreeValue(context_, result_);
        result_ =
            JS_ThrowTypeError(context_, "%.*s", static_cast<int>(message.size()), message.data());
    }
    [[nodiscard]] JSValue release() noexcept
    {
        return std::exchange(result_, JS_UNDEFINED);
    }

private:
    [[nodiscard]] bool in_range(int index) const noexcept
    {
        return index >= 0 && index < count_;
    }
    JSContext* context_;
    int count_;
    JSValueConst* values_;
    void* owner_;
    std::int32_t data_;
    mutable JSValue result_ = JS_UNDEFINED;
};

class QuickJsVm final : public ScriptVm
{
public:
    QuickJsVm() = default;
    QuickJsVm(const QuickJsVm&) = delete;
    QuickJsVm& operator=(const QuickJsVm&) = delete;
    QuickJsVm(QuickJsVm&&) = delete;
    QuickJsVm& operator=(QuickJsVm&&) = delete;
    ~QuickJsVm() noexcept override
    {
        if (context_ != nullptr)
        {
            JS_SetContextOpaque(context_, nullptr);
            JS_FreeValue(context_, gameplay_);
            JS_FreeValue(context_, native_);
            for (const auto& rejection : rejections_)
            {
                JS_FreeValue(context_, rejection.first);
            }
            for (const auto& [path, module] : modules_)
            {
                static_cast<void>(path);
                JS_FreeValue(context_, module);
            }
            JS_FreeContext(context_);
        }
        if (runtime_ != nullptr)
        {
            JS_FreeRuntime(runtime_);
        }
    }

    [[nodiscard]] bool initialize(void* owner) override
    {
        if (runtime_ != nullptr)
        {
            return fail("Script virtual machine is already initialized");
        }
        owner_ = owner;
        runtime_ = JS_NewRuntime();
        if (runtime_ == nullptr)
        {
            return fail("Cannot create QuickJS runtime");
        }
        JS_SetMaxStackSize(runtime_, 1024 * 1024);
        JS_SetModuleLoaderFunc(runtime_, normalize, load, this);
        JS_SetHostPromiseRejectionTracker(runtime_, rejection, this);
        context_ = JS_NewContext(runtime_);
        if (context_ == nullptr)
        {
            return fail("Cannot create QuickJS context");
        }
        JS_SetContextOpaque(context_, this);
        native_ = JS_NewObject(context_);
        return !JS_IsException(native_) || exception("Cannot create native binding object");
    }

    [[nodiscard]] bool function(std::string_view name, NativeCallback callback,
                                std::int32_t data) override
    {
        if (!can_register() || callback == nullptr ||
            functions_.size() >= static_cast<std::size_t>(INT32_MAX))
        {
            return fail("Invalid native function registration");
        }
        JSValue index = JS_NewInt32(context_, static_cast<std::int32_t>(functions_.size()));
        functions_.push_back({callback, data});
        return define(name, JS_NewCFunctionData(context_, invoke, 0, 0, 1, &index));
    }
    [[nodiscard]] bool integer(std::string_view name, std::int32_t value) override
    {
        if (!can_register())
        {
            return fail("Native constant registration requires an unsealed initialized VM");
        }
        return define(name, JS_NewInt32(context_, value));
    }
    [[nodiscard]] bool string(std::string_view name, std::string_view value) override
    {
        if (!can_register() || value.size() > maximum_text_size)
        {
            return fail("Native string exceeds the size limit");
        }
        return define(name, JS_NewStringLen(context_, value.data(), value.size()));
    }
    [[nodiscard]] bool seal_bindings() override
    {
        if (!can_register() || JS_PreventExtensions(context_, native_) < 0)
        {
            return fail("Cannot freeze native bindings");
        }
        Value global(context_, JS_GetGlobalObject(context_));
        if (JS_DefinePropertyValueStr(context_, global.get(), "__engineNative",
                                      JS_DupValue(context_, native_), 0) < 0)
        {
            return exception("Cannot expose native bindings");
        }
        sealed_ = true;
        return true;
    }

    [[nodiscard]] bool prepare(std::string_view entry, SourceLoader loader) override
    {
        if (!sealed_ || !modules_.empty() || !loader || entry.empty())
        {
            return fail("Invalid gameplay preparation state");
        }
        loader_ = std::move(loader);
        entry_ = entry;
        preparing_ = true;
        const auto engine = compile("engine");
        const auto module = JS_IsException(engine) ? JS_EXCEPTION : compile(entry_);
        const bool linked = !JS_IsException(module) &&
                            JS_InstantiateModule(context_, engine) == 0 &&
                            JS_InstantiateModule(context_, module) == 0;
        const int async = linked ? JS_IsModuleGraphAsync(context_, module) : -1;
        preparing_ = false;
        if (!linked || async < 0)
        {
            return exception("Gameplay module precheck failed");
        }
        if (async != 0)
        {
            return fail("Top-level await is not supported by gameplay lifecycle");
        }
        return true;
    }

    [[nodiscard]] bool start() override
    {
        error_.clear();
        const auto module = modules_.find(entry_);
        if (module == modules_.end() || evaluated_)
        {
            return fail("No prepared gameplay module");
        }
        const auto engine_module = modules_.find("engine");
        if (engine_module == modules_.end() || !evaluate(engine_module->second))
        {
            return false;
        }
        evaluated_ = true;
        if (!evaluate(module->second))
        {
            return false;
        }
        Value exports(context_, module_namespace(module->second));
        Value engine(context_, module_namespace(engine_module->second));
        Value constructor(context_, JS_GetPropertyStr(context_, exports.get(), "default"));
        Value base(context_, JS_GetPropertyStr(context_, engine.get(), "Gameplay"));
        if (JS_IsException(constructor.get()) || JS_IsException(base.get()) ||
            !JS_IsFunction(context_, constructor.get()))
        {
            return exception("Entry must default-export a class extending Gameplay");
        }
        Value object(context_, JS_CallConstructor(context_, constructor.get(), 0, nullptr));
        if (JS_IsException(object.get()) ||
            JS_IsInstanceOf(context_, object.get(), base.get()) != 1)
        {
            return exception("Entry must default-export a class extending Gameplay");
        }
        gameplay_ = object.release();
        return true;
    }
    [[nodiscard]] bool call_gameplay(std::string_view name, bool require_true) override
    {
        return call(gameplay_, name, require_true);
    }
    [[nodiscard]] bool call_binding(std::string_view name) override
    {
        if (context_ == nullptr)
        {
            return fail("Script VM is not initialized");
        }
        if (!evaluated_)
        {
            return true;
        }
        const auto module = modules_.find("engine");
        if (module == modules_.end())
        {
            return fail("Engine binding module is unavailable");
        }
        Value engine(context_, module_namespace(module->second));
        return call(engine.get(), name, false);
    }
    [[nodiscard]] bool drain_jobs() override
    {
        if (runtime_ == nullptr || context_ == nullptr)
        {
            return fail("Script VM is not initialized");
        }
        JSContext* context = nullptr;
        while (JS_IsJobPending(runtime_))
        {
            if (JS_ExecutePendingJob(runtime_, &context) < 0)
            {
                return exception("Script job failed");
            }
        }
        if (!async_error_.empty())
        {
            return fail(async_error_);
        }
        return rejections_.empty() || fail(rejections_.front().second);
    }
    [[nodiscard]] const std::string& last_error() const noexcept override
    {
        return error_;
    }
    [[nodiscard]] const std::vector<std::string>& loaded_modules() const noexcept override
    {
        return module_names_;
    }

private:
    [[nodiscard]] bool can_register() const noexcept
    {
        return context_ != nullptr && !sealed_ && !JS_IsException(native_);
    }

    struct Function
    {
        NativeCallback callback;
        std::int32_t data;
    };

    [[nodiscard]] bool fail(std::string message)
    {
        error_ = std::move(message);
        return false;
    }
    [[nodiscard]] bool exception(std::string operation)
    {
        Value caught(context_, JS_GetException(context_));
        if (!JS_IsNull(caught.get()) && !JS_IsUndefined(caught.get()))
        {
            operation += ": " + value_text(context_, caught.get());
            if (JS_IsObject(caught.get()))
            {
                Value stack(context_, JS_GetPropertyStr(context_, caught.get(), "stack"));
                if (JS_IsString(stack.get()))
                {
                    operation += "\n" + value_text(context_, stack.get());
                }
            }
        }
        return fail(std::move(operation));
    }
    [[nodiscard]] bool define(std::string_view name, JSValue value)
    {
        Value owned(context_, value);
        if (sealed_ || name.empty() || name.find('\0') != std::string_view::npos ||
            JS_IsException(owned.get()))
        {
            return fail("Invalid native binding");
        }
        const std::string key(name);
        const JSAtom atom = JS_NewAtomLen(context_, name.data(), name.size());
        const int exists = JS_HasProperty(context_, native_, atom);
        JS_FreeAtom(context_, atom);
        if (exists != 0)
        {
            return fail("Duplicate native binding: " + key);
        }
        return JS_DefinePropertyValueStr(context_, native_, key.c_str(), owned.release(),
                                         JS_PROP_ENUMERABLE) >= 0 ||
               exception("Cannot define native binding " + key);
    }
    [[nodiscard]] JSValueConst compile(const std::string& path)
    {
        const auto found = modules_.find(path);
        if (found != modules_.end())
        {
            return found->second;
        }
        if (module_names_.size() >= maximum_module_count)
        {
            return JS_ThrowRangeError(context_, "Module graph exceeds 256 files");
        }
        // 在解析前登记计数；循环依赖由 QuickJS 内部模块表解析，不重复执行 loader。
        module_names_.push_back(path);
        std::string source;
        if (!loader_(path, source) || source.size() > maximum_text_size)
        {
            return JS_ThrowReferenceError(context_, "Cannot read module: %s", path.c_str());
        }
        const JSValue module = JS_Eval(context_, source.c_str(), source.size(), path.c_str(),
                                       JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
        if (!JS_IsException(module))
        {
            modules_.emplace(path, module);
        }
        return module;
    }
    [[nodiscard]] JSValue module_namespace(JSValueConst module)
    {
        return JS_GetModuleNamespace(context_, static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(module)));
    }
    [[nodiscard]] bool evaluate(JSValueConst module)
    {
        Value result(context_, JS_EvalFunction(context_, JS_DupValue(context_, module)));
        if (JS_IsException(result.get()))
        {
            return exception("Gameplay module execution failed");
        }
        if (JS_IsPromise(result.get()) &&
            JS_PromiseState(context_, result.get()) != JS_PROMISE_FULFILLED)
        {
            Value reason(context_, JS_PromiseResult(context_, result.get()));
            return fail("Gameplay module execution failed: " + value_text(context_, reason.get()));
        }
        return true;
    }
    [[nodiscard]] bool call(JSValueConst object, std::string_view name, bool require_true)
    {
        error_.clear();
        if (context_ == nullptr || !JS_IsObject(object))
        {
            return fail("Script callback target is unavailable");
        }
        const std::string key(name);
        Value member(context_, JS_GetPropertyStr(context_, object, key.c_str()));
        if (JS_IsException(member.get()) || !JS_IsFunction(context_, member.get()))
        {
            return exception("Missing script callback: " + key);
        }
        Value result(context_, JS_Call(context_, member.get(), object, 0, nullptr));
        if (JS_IsException(result.get()))
        {
            return exception("Script callback " + key + " failed");
        }
        if (JS_IsPromise(result.get()))
        {
            return fail("Script callback " + key + " must be synchronous");
        }
        if (require_true && (!JS_IsBool(result.get()) || JS_ToBool(context_, result.get()) != 1))
        {
            return fail("Gameplay.OnInitialize must return true to start");
        }
        return true;
    }
    static JSValue invoke(JSContext* context, JSValueConst, int count, JSValueConst* values, int,
                          JSValue* data)
    {
        auto* machine = static_cast<QuickJsVm*>(JS_GetContextOpaque(context));
        std::int32_t index = -1;
        if (machine == nullptr || JS_ToInt32(context, &index, data[0]) < 0 || index < 0 ||
            static_cast<std::size_t>(index) >= machine->functions_.size())
        {
            return JS_ThrowInternalError(context, "Native function owner is unavailable");
        }
        const auto& function = machine->functions_[static_cast<std::size_t>(index)];
        QuickJsArguments arguments(context, count, values, machine->owner_, function.data);
        function.callback(arguments);
        return arguments.release();
    }
    static char* normalize(JSContext* context, const char* base, const char* name, void* opaque)
    {
        auto& machine = *static_cast<QuickJsVm*>(opaque);
        if (!machine.preparing_)
        {
            machine.async_error_ = "Dynamic import is not supported";
            (void)JS_ThrowTypeError(context, "Dynamic import is not supported");
            return nullptr;
        }
        std::string path;
        if (!resolve_module_path(base, name, path))
        {
            (void)JS_ThrowReferenceError(context, "Invalid module import: %s", name);
            return nullptr;
        }
        return js_strndup(context, path.data(), path.size());
    }
    static JSModuleDef* load(JSContext*, const char* name, void* opaque)
    {
        auto& machine = *static_cast<QuickJsVm*>(opaque);
        const JSValueConst module = machine.compile(name);
        return JS_IsException(module) ? nullptr
                                      : static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(module));
    }
    static void rejection(JSContext* context, JSValueConst promise, JSValueConst reason,
                          bool handled, void* opaque)
    {
        auto& machine = *static_cast<QuickJsVm*>(opaque);
        const auto found = std::find_if(
            machine.rejections_.begin(), machine.rejections_.end(), [promise](const auto& rejected)
            { return JS_VALUE_GET_PTR(rejected.first) == JS_VALUE_GET_PTR(promise); });
        if (handled)
        {
            if (found != machine.rejections_.end())
            {
                JS_FreeValue(context, found->first);
                machine.rejections_.erase(found);
            }
        }
        else if (found == machine.rejections_.end())
        {
            machine.rejections_.emplace_back(JS_DupValue(context, promise),
                                             "Unhandled script promise: " +
                                                 value_text(context, reason));
        }
    }

    JSRuntime* runtime_ = nullptr;
    JSContext* context_ = nullptr;
    void* owner_ = nullptr;
    JSValue native_ = JS_UNDEFINED;
    JSValue gameplay_ = JS_UNDEFINED;
    std::vector<Function> functions_;
    std::map<std::string, JSValue> modules_;
    std::vector<std::string> module_names_;
    SourceLoader loader_;
    std::string entry_;
    std::string error_;
    std::string async_error_;
    std::vector<std::pair<JSValue, std::string>> rejections_;
    bool sealed_ = false;
    bool preparing_ = false;
    bool evaluated_ = false;
};
} // namespace

std::unique_ptr<ScriptVm> create_script_vm()
{
    return std::make_unique<QuickJsVm>();
}

} // namespace engine::plugins::script::vm
