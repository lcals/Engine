#pragma once

#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "runtime/scene/node.hpp"

namespace engine::plugins::script::bindings::binding_detail
{
template <class T> [[nodiscard]] T component(const Arguments& arguments, int expected_count)
{
    arguments.set_return(false);
    if (arguments.count() != expected_count)
    {
        return {};
    }
    const auto node = BindingContext::node(arguments);
    return node ? node.get_component<T>() : T{};
}

template <class T> void add_component(const Arguments& arguments)
{
    arguments.set_return(false);
    if (arguments.count() != 2)
    {
        return;
    }
    auto node = BindingContext::node(arguments);
    arguments.set_return(node && node.add_component<T>().valid());
}
} // namespace engine::plugins::script::bindings::binding_detail
