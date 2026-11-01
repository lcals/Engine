#include "plugins/runtime/script/bindings/storage_binding.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"

namespace engine::plugins::script::bindings
{
namespace
{
constexpr std::string_view javascript = R"js(
export class Storage {
    constructor() { throw new TypeError('Storage is static'); }
    static GetString(...args) { return native.StorageGetString(...args); }
    static SetString(...args) { return native.StorageSetString(...args); }
    static HasKey(...args) { return native.StorageHasKey(...args); }
    static DeleteKey(...args) { return native.StorageDeleteKey(...args); }
    static Save(...args) { return native.StorageSave(...args); }
    static get isReady() { return native.StorageIsReady(); }
    static get lastError() { return native.StorageLastError(); }
}
Object.freeze(Storage.prototype);
Object.freeze(Storage);
)js";

BindingContext* begin_call(const Arguments& arguments, int count, std::string_view signature)
{
    auto* context = BindingContext::current(arguments);
    if (context == nullptr)
    {
        return nullptr;
    }
    if (arguments.count() != count)
    {
        context->set_storage_error("Invalid argument count for " + std::string(signature));
        return nullptr;
    }
    context->set_storage_error({});
    return context;
}

bool read_text(BindingContext& context, const Arguments& arguments, int index, std::string& output)
{
    if (!BindingContext::text(arguments, index, output))
    {
        context.set_storage_error("Storage arguments must be strings of at most 4 MiB");
        return false;
    }
    return true;
}

void get_string(const Arguments& arguments)
{
    arguments.set_return({});
    auto* context = begin_call(arguments, 1, "Storage.GetString(key)");
    std::string key;
    std::string value;
    if (context != nullptr && read_text(*context, arguments, 0, key) &&
        context->storage().get_string(key, value))
    {
        arguments.set_return(value);
    }
}

void set_string(const Arguments& arguments)
{
    arguments.set_return(false);
    auto* context = begin_call(arguments, 2, "Storage.SetString(key, value)");
    std::string key;
    std::string value;
    if (context != nullptr && read_text(*context, arguments, 0, key) &&
        read_text(*context, arguments, 1, value))
    {
        arguments.set_return(context->storage().set_string(key, value));
    }
}

void has_key(const Arguments& arguments)
{
    arguments.set_return(false);
    auto* context = begin_call(arguments, 1, "Storage.HasKey(key)");
    std::string key;
    if (context != nullptr && read_text(*context, arguments, 0, key))
    {
        arguments.set_return(context->storage().has_key(key));
    }
}

void delete_key(const Arguments& arguments)
{
    arguments.set_return(false);
    auto* context = begin_call(arguments, 1, "Storage.DeleteKey(key)");
    std::string key;
    if (context != nullptr && read_text(*context, arguments, 0, key))
    {
        arguments.set_return(context->storage().delete_key(key));
    }
}

void save(const Arguments& arguments)
{
    arguments.set_return(false);
    auto* context = begin_call(arguments, 0, "Storage.Save()");
    if (context != nullptr)
    {
        arguments.set_return(context->storage().save());
    }
}

void is_ready(const Arguments& arguments)
{
    arguments.set_return(false);
    auto* context = BindingContext::current(arguments);
    if (context != nullptr && arguments.count() == 0)
    {
        arguments.set_return(context->storage().is_loaded());
    }
}

void last_error(const Arguments& arguments)
{
    arguments.set_return({});
    auto* context = BindingContext::current(arguments);
    if (context != nullptr && arguments.count() == 0)
    {
        arguments.set_return(context->storage_error());
    }
}
} // namespace

std::string_view StorageBinding::name() const noexcept
{
    return "Storage";
}

std::string_view StorageBinding::source() const noexcept
{
    return javascript;
}

bool StorageBinding::register_functions(vm::ScriptVm& vm) const
{
    return vm.function("StorageGetString", get_string) &&
           vm.function("StorageSetString", set_string) && vm.function("StorageHasKey", has_key) &&
           vm.function("StorageDeleteKey", delete_key) && vm.function("StorageSave", save) &&
           vm.function("StorageIsReady", is_ready) && vm.function("StorageLastError", last_error);
}

} // namespace engine::plugins::script::bindings
