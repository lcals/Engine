#include "plugins/runtime/script/bindings/resources_binding.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"

namespace engine::plugins::script::bindings
{
namespace
{
using Value = vm::NativeValue;
constexpr std::string_view javascript = R"js(
const resourceHandles = new WeakMap();
const resourceRequests = new WeakMap();
function resourceToken(value) { return resourceHandles.get(value) ?? 0n; }
function resourceHandle(token) {
    if (token === null || token === 0n) return null;
    const value = Object.freeze({});
    resourceHandles.set(value, token);
    return value;
}
export const ResourceLoadStatus = Object.freeze({ loading: 'loading', ready: 'ready', delivered: 'delivered', failed: 'failed', cancelled: 'cancelled' });
export class ResourceRequest {
    constructor(token, request) {
        if (token !== key) throw new TypeError('Use Resources.BeginLoad');
        resourceRequests.set(this, { id: request, terminal: null });
        Object.freeze(this);
    }
    Status() { const r = resourceRequests.get(this); return (r.terminal ?? native.ResourceRequestStatus(r.id))?.status ?? 'failed'; }
    Progress() { const r = resourceRequests.get(this); const s = r.terminal ?? native.ResourceRequestStatus(r.id); return s ? { completed: s.completed, total: s.total } : { completed: 0, total: 0 }; }
    Error() { const r = resourceRequests.get(this); return (r.terminal ?? native.ResourceRequestStatus(r.id))?.error ?? 'Invalid resource request'; }
    Take() {
        const r = resourceRequests.get(this);
        if (r.terminal) return null;
        const s = native.ResourceRequestStatus(r.id);
        const value = resourceHandle(native.ResourceTake(r.id));
        if (value) r.terminal = { ...s, status: 'delivered' };
        return value;
    }
    Cancel() {
        const r = resourceRequests.get(this);
        if (r.terminal) return false;
        const s = native.ResourceRequestStatus(r.id);
        const result = native.ResourceCancel(r.id);
        r.terminal = result ? { ...s, status: 'cancelled', error: 'Resource request cancelled' } : s;
        return result;
    }
}
export class Resources {
    constructor() { throw new TypeError('Resources is static'); }
    static Load(path) { return resourceHandle(native.ResourceLoad(path)); }
    static BeginLoad(path) { const id = native.ResourceBeginLoad(path); return id ? new ResourceRequest(key, id) : null; }
    static Retain(handle) { return resourceHandle(native.ResourceRetain(resourceToken(handle))); }
    static Release(handle) { const result = native.ResourceRelease(resourceToken(handle)); if (result) resourceHandles.delete(handle); return result; }
    static get lastError() { return native.ResourceLastError(); }
}
Object.freeze(ResourceRequest.prototype);
Object.freeze(ResourceRequest);
Object.freeze(Resources.prototype);
Object.freeze(Resources);
)js";

void load(const Arguments& arguments)
{
    arguments.set_return({});
    auto* context = BindingContext::current(arguments);
    std::string path;
    if (!context || arguments.count() != 1 || !BindingContext::text(arguments, 0, path))
    {
        return;
    }
    const auto token =
        arguments.data() ? context->begin_resource_load(path) : context->load_resource(path);
    if (token)
    {
        arguments.set_return(token);
    }
}

void operation(const Arguments& arguments)
{
    arguments.set_return({});
    auto* context = BindingContext::current(arguments);
    std::uint64_t token = 0;
    if (!context || arguments.count() != 1 || !BindingContext::identifier(arguments, 0, token))
    {
        return;
    }
    switch (arguments.data())
    {
    case 0:
    {
        const auto result = context->retain_resource(token);
        if (result)
        {
            arguments.set_return(result);
        }
        return;
    }
    case 1:
        arguments.set_return(context->release_resource(token));
        return;
    case 2:
    {
        const auto result = context->take_resource(token);
        if (result)
        {
            arguments.set_return(result);
        }
        return;
    }
    case 3:
        arguments.set_return(context->cancel_resource_load(token));
        return;
    default:
        return;
    }
}

void status(const Arguments& arguments)
{
    arguments.set_return({});
    auto* context = BindingContext::current(arguments);
    std::uint64_t token = 0;
    if (!context || arguments.count() != 1 || !BindingContext::identifier(arguments, 0, token))
    {
        return;
    }
    auto* request = context->resource_request(token);
    if (!request)
    {
        return;
    }
    ResourceLoadProgress progress;
    const auto state = request->status(progress);
    const char* names[] = {"loading", "ready", "delivered", "failed", "cancelled"};
    arguments.set_return(Value::Object{{"status", names[static_cast<unsigned>(state)]},
                                       {"completed", progress.completed},
                                       {"total", progress.total},
                                       {"error", request->last_error()}});
}

void last_error(const Arguments& arguments)
{
    const auto* context = BindingContext::current(arguments);
    arguments.set_return(context ? Value(context->resource_error()) : Value{});
}
} // namespace

std::string_view ResourcesBinding::name() const noexcept
{
    return "Resources";
}
std::string_view ResourcesBinding::source() const noexcept
{
    return javascript;
}
bool ResourcesBinding::register_functions(vm::ScriptVm& vm) const
{
    return vm.function("ResourceLoad", load, 0) && vm.function("ResourceBeginLoad", load, 1) &&
           vm.function("ResourceRetain", operation, 0) &&
           vm.function("ResourceRelease", operation, 1) &&
           vm.function("ResourceTake", operation, 2) &&
           vm.function("ResourceCancel", operation, 3) &&
           vm.function("ResourceRequestStatus", status) &&
           vm.function("ResourceLastError", last_error);
}
} // namespace engine::plugins::script::bindings
