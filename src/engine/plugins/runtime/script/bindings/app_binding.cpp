#include "plugins/runtime/script/bindings/app_binding.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "runtime/app/app.hpp"
#include "runtime/utils/path_utils.hpp"

namespace engine::plugins::script::bindings
{
namespace
{
constexpr std::string_view javascript = R"js(
export class App {
    constructor() { throw new TypeError('App is static'); }
    static get builtinAssetsPath() { return native.builtinAssetsPath; }
    static get persistentDataPath() { return native.persistentDataPath; }
    static get cachePath() { return native.cachePath; }
}
Object.freeze(App.prototype);
Object.freeze(App);
)js";

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
    return vm.string("builtinAssetsPath", PathUtils::to_utf8(App::builtin_assets_path())) &&
           vm.string("persistentDataPath", PathUtils::to_utf8(App::persistent_data_path())) &&
           vm.string("cachePath", PathUtils::to_utf8(App::cache_path()));
}

} // namespace engine::plugins::script::bindings
