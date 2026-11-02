#include "plugins/runtime/script/bindings/completion_binding.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"

namespace engine::plugins::script::bindings
{
namespace
{
constexpr std::string_view javascript = R"js(
const asyncCompletions = new Map();
function submitCompletion(start, completed) {
    if (typeof completed !== 'function') return false;
    const id = start();
    if (typeof id !== 'bigint' || id === 0n) return false;
    asyncCompletions.set(id, completed);
    return true;
}
bindingEventHooks.push(() => {
    for (const [id, completed] of [...asyncCompletions]) {
        const status = native.AsyncCompletion(id);
        if (status === -1) continue;
        asyncCompletions.delete(id);
        if (status < 0) continue;
        if (completed(status === 1) instanceof Promise)
            throw new TypeError('Completion callbacks must be synchronous');
    }
});
bindingCleanupHooks.push(() => asyncCompletions.clear());
)js";

void poll(const Arguments& arguments)
{
    auto* context = BindingContext::current(arguments);
    std::uint64_t request = 0;
    arguments.set_return(context != nullptr && arguments.count() == 1 &&
                                 BindingContext::identifier(arguments, 0, request)
                             ? context->take_async(request)
                             : -2);
}
} // namespace

std::string_view CompletionBinding::name() const noexcept
{
    return "Completion";
}

std::string_view CompletionBinding::source() const noexcept
{
    return javascript;
}

bool CompletionBinding::register_functions(vm::ScriptVm& vm) const
{
    return vm.function("AsyncCompletion", poll);
}
} // namespace engine::plugins::script::bindings
