#include "plugins/runtime/script/bindings/app_binding.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "runtime/app/app.hpp"
#include "runtime/utils/path_utils.hpp"
#include "runtime/window/window_system.hpp"

namespace engine::plugins::script::bindings
{
namespace
{
constexpr std::string_view javascript = R"js(
export class App {
    constructor() { throw new TypeError('App is static'); }
    static get assetsPath() { return native.assetsPath; }
    static get persistentDataPath() { return native.persistentDataPath; }
    static get cachePath() { return native.cachePath; }
    static RequestQuit() { return arguments.length === 0 && native.AppRequestQuit(); }
}
Object.freeze(App.prototype);
Object.freeze(App);
)js";

void request_quit(const Arguments& arguments)
{
    arguments.set_return(false);
    auto* context = BindingContext::current(arguments);
    auto* window = context != nullptr ? context->window() : nullptr;
    if (window != nullptr && arguments.count() == 0)
    {
        arguments.set_return(window->request_exit());
    }
}

} // namespace

std::string_view AppBinding::name() const noexcept
{
    return "App";
}

std::string_view AppBinding::source() const noexcept
{
    return javascript;
}

bool AppBinding::register_functions(vm::ScriptVm& vm) const
{
    return vm.string("assetsPath", PathUtils::to_utf8(App::assets_path())) &&
           vm.string("persistentDataPath", PathUtils::to_utf8(App::persistent_data_path())) &&
           vm.string("cachePath", PathUtils::to_utf8(App::cache_path())) &&
           vm.function("AppRequestQuit", request_quit);
}

} // namespace engine::plugins::script::bindings
