#include "plugins/runtime/script/bindings/json_binding.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "runtime/resource/types/json_resource.hpp"

#include <nlohmann/json.hpp>

namespace engine::plugins::script::bindings
{
namespace
{
constexpr std::string_view javascript = R"js(
export class Json {
    constructor() { throw new TypeError('Json is static'); }
    static ParseResource(resource) {
        const text = native.JsonReadResource(resourceToken(resource));
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
    auto* context = BindingContext::current(arguments);
    std::uint64_t token = 0;
    const JsonResource* resource = nullptr;
    if (!context || arguments.count() != 1 || !BindingContext::identifier(arguments, 0, token) ||
        !context->resources() ||
        context->resources()->get(context->resource_handle(token), resource) != ResourceError::none)
    {
        return;
    }
    const auto& bytes = resource->data();
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
    return vm.function("JsonReadResource", read);
}

} // namespace engine::plugins::script::bindings
