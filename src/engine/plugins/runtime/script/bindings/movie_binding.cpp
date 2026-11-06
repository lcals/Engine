#include "plugins/runtime/script/bindings/movie_binding.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"

namespace engine::plugins::script::bindings
{
namespace
{
using Value = vm::NativeValue;
constexpr std::string_view javascript = R"js(
export const MovieState = Object.freeze({ preparing: 0, playing: 1, paused: 2, completed: 3, cancelled: 4, failed: 5 });
export const MovieStopReason = Object.freeze({ none: 0, stopped: 1, skipped: 2 });
export class Movie {
    constructor() { throw new TypeError('Movie is static'); }
    static Play(path) { return native.MoviePlay(path); }
    static SetPaused(handle, paused) { return native.MovieSetPaused(handle?.id, handle?.generation, paused); }
    static Stop(handle, reason = MovieStopReason.stopped) { return native.MovieStop(handle?.id, handle?.generation, reason); }
    static Status(handle) { return native.MovieStatus(handle?.id, handle?.generation); }
    static Release(handle) { return native.MovieRelease(handle?.id, handle?.generation); }
    static get lastError() { return native.MovieLastError(); }
}
Object.freeze(Movie.prototype);
Object.freeze(Movie);
)js";

bool get_handle(const Arguments& arguments, BindingContext*& context, MoviePlaybackHandle& handle)
{
    context = BindingContext::current(arguments);
    return context && context->movies() && arguments.count() >= 2 &&
           BindingContext::identifier(arguments, 0, handle.id) &&
           BindingContext::identifier(arguments, 1, handle.generation) &&
           context->owns_movie(handle);
}
void play(const Arguments& arguments)
{
    auto* context = BindingContext::current(arguments);
    std::string path;
    MoviePlaybackHandle handle;
    if (!context || !context->movies() || arguments.count() != 1 ||
        !BindingContext::text(arguments, 0, path) || !context->movies()->play(path, handle))
    {
        arguments.set_return({});
        return;
    }
    if (!context->remember_movie(handle))
    {
        (void)context->movies()->stop(handle, MovieStopReason::stopped);
        (void)context->movies()->release(handle);
        arguments.set_return({});
        return;
    }
    arguments.set_return(Value::Object{{"id", handle.id}, {"generation", handle.generation}});
}
void set_paused(const Arguments& arguments)
{
    BindingContext* context = nullptr;
    MoviePlaybackHandle handle;
    arguments.set_return(get_handle(arguments, context, handle) && arguments.count() == 3 &&
                         arguments.is_boolean(2) &&
                         context->movies()->set_paused(handle, arguments.boolean(2)));
}
void stop(const Arguments& arguments)
{
    BindingContext* context = nullptr;
    MoviePlaybackHandle handle;
    arguments.set_return(
        get_handle(arguments, context, handle) && arguments.count() == 3 && arguments.is_int32(2) &&
        arguments.integer(2) >= 1 && arguments.integer(2) <= 2 &&
        context->movies()->stop(handle, static_cast<MovieStopReason>(arguments.integer(2))));
}
void status(const Arguments& arguments)
{
    BindingContext* context = nullptr;
    MoviePlaybackHandle handle;
    MoviePlaybackStatus state;
    if (!get_handle(arguments, context, handle) || arguments.count() != 2 ||
        !context->movies()->status(handle, state))
    {
        arguments.set_return({});
        return;
    }
    arguments.set_return(Value::Object{{"state", static_cast<std::int32_t>(state.state)},
                                       {"stopReason", static_cast<std::int32_t>(state.stop_reason)},
                                       {"positionSeconds", state.position_seconds},
                                       {"durationSeconds", state.duration_seconds},
                                       {"error", state.error}});
}
void release(const Arguments& arguments)
{
    BindingContext* context = nullptr;
    MoviePlaybackHandle handle;
    const bool success = get_handle(arguments, context, handle) && arguments.count() == 2 &&
                         context->movies()->release(handle);
    if (success)
    {
        context->forget_movie(handle);
    }
    arguments.set_return(success);
}
void last_error(const Arguments& arguments)
{
    const auto* context = BindingContext::current(arguments);
    if (!context || !context->movies() || arguments.count() != 0)
    {
        arguments.set_return({});
        return;
    }
    arguments.set_return(context->movies()->last_error());
}
} // namespace
std::string_view MovieBinding::name() const noexcept
{
    return "Movie";
}
std::string_view MovieBinding::source() const noexcept
{
    return javascript;
}
bool MovieBinding::register_functions(vm::ScriptVm& vm) const
{
    return vm.function("MoviePlay", play) && vm.function("MovieSetPaused", set_paused) &&
           vm.function("MovieStop", stop) && vm.function("MovieStatus", status) &&
           vm.function("MovieRelease", release) && vm.function("MovieLastError", last_error);
}
} // namespace engine::plugins::script::bindings
