#include "plugins/runtime/script/bindings/json_binding.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "runtime/resource/resource_system.hpp"

#include <nlohmann/json.hpp>

namespace engine::plugins::script::bindings
{
namespace
{
constexpr std::string_view javascript = R"js(
export class Json {
    constructor() { throw new TypeError('Json is static'); }
    static Load(path) {
        if (arguments.length !== 1 || typeof path !== 'string') return null;
        const text = native.JsonRead(path);
        if (text === null) return null;
        try { return JSON.parse(text); }
        catch { return null; }
    }
}
Object.freeze(Json.prototype);
Object.freeze(Json);
)js";

void read(const Arguments& arguments)
{
    arguments.set_return({});
    std::string path;
    if (BindingContext::current(arguments) == nullptr || arguments.count() != 1 ||
        !BindingContext::text(arguments, 0, path))
    {
        return;
    }
    auto* system = ResourceSystem::instance();
    Guid guid;
    ResourcePtr resource;
    if (system == nullptr || !ResourceSystem::path_to_guid(path, guid) ||
        !system->loader().load(guid, resource) || resource == nullptr ||
        resource->info().kind != schema::AssetKind::json)
    {
        return;
    }
    const auto bytes = resource->bytes();
    if (bytes.empty() || bytes.size() > vm::maximum_text_size)
    {
        return;
    }
    const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    // Validate before crossing the text bridge so invalid UTF-8 cannot become replacement glyphs.
    if (text.starts_with("\xef\xbb\xbf") || text.find('\0') != std::string_view::npos ||
        !nlohmann::json::accept(text))
    {
        return;
    }
    arguments.set_return(text);
}
} // namespace

std::string_view JsonBinding::name() const noexcept
{
    return "Json";
}

std::string_view JsonBinding::source() const noexcept
{
    return javascript;
}

bool JsonBinding::register_functions(vm::ScriptVm& vm) const
{
    return vm.function("JsonRead", read);
}

} // namespace engine::plugins::script::bindings
