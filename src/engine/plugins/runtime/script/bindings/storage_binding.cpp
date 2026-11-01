#include "plugins/runtime/script/bindings/storage_binding.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"

namespace engine::plugins::script::bindings
{
namespace
{
constexpr std::string_view javascript = R"js(
export class Storage {
    constructor() { throw new TypeError('Storage is static'); }
    static GetString(key) { return native.StorageGetString(key); }
    static SetString(key, value) { return native.StorageSetString(key, value); }
    static HasKey(key) { return native.StorageHasKey(key); }
    static DeleteKey(key) { return native.StorageDeleteKey(key); }
    static Save() { return native.StorageSave(); }
    static get isReady() { return native.StorageIsReady(); }
    static get lastError() { return native.StorageLastError(); }
}
Object.freeze(Storage.prototype);
Object.freeze(Storage);
)js";

void get_string(const Arguments& arguments)
{
    arguments.set_return({});
    auto* context = BindingContext::current(arguments);
    std::string key;
    std::string value;
    if (context != nullptr && arguments.count() == 1 && BindingContext::text(arguments, 0, key) &&
        context->storage().get_string(key, value))
    {
        arguments.set_return(value);
    }
}

void set_string(const Arguments& arguments)
{
    arguments.set_return(false);
    auto* context = BindingContext::current(arguments);
    std::string key;
    std::string value;
    if (context != nullptr && arguments.count() == 2 && BindingContext::text(arguments, 0, key) &&
        BindingContext::text(arguments, 1, value))
    {
        arguments.set_return(context->storage().set_string(key, value));
    }
}

void has_key(const Arguments& arguments)
{
    arguments.set_return(false);
    auto* context = BindingContext::current(arguments);
    std::string key;
    if (context != nullptr && arguments.count() == 1 && BindingContext::text(arguments, 0, key))
    {
        arguments.set_return(context->storage().has_key(key));
    }
}

void delete_key(const Arguments& arguments)
{
    arguments.set_return(false);
    auto* context = BindingContext::current(arguments);
    std::string key;
    if (context != nullptr && arguments.count() == 1 && BindingContext::text(arguments, 0, key))
    {
        arguments.set_return(context->storage().delete_key(key));
    }
}

void save(const Arguments& arguments)
{
    arguments.set_return(false);
    auto* context = BindingContext::current(arguments);
    if (context != nullptr && arguments.count() == 0)
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
        arguments.set_return(context->storage().last_error());
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
