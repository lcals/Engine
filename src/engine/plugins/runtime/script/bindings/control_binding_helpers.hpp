#pragma once
#include "plugins/runtime/script/bindings/binding_helpers.hpp"
#include "runtime/schema/control_json_generated.hpp"
namespace engine::plugins::script::bindings::control_detail
{
using Json = nlohmann::json;
inline vm::NativeValue native(const Json& value)
{
    if (value.is_boolean())
    {
        return value.get<bool>();
    }
    if (value.is_number_unsigned())
    {
        return value.get<std::uint64_t>();
    }
    if (value.is_number())
    {
        return value.get<double>();
    }
    if (value.is_string())
    {
        return value.get<std::string>();
    }
    if (value.is_array())
    {
        vm::NativeValue::Array out;
        for (const auto& item : value)
        {
            out.push_back(native(item));
        }
        return out;
    }
    if (value.is_object())
    {
        vm::NativeValue::Object out;
        for (auto i = value.begin(); i != value.end(); ++i)
        {
            out.emplace_back(i.key(), native(i.value()));
        }
        return out;
    }
    return {};
}
inline bool argument(const Arguments& a, int index, Json& out)
{
    std::string text;
    if (!BindingContext::text(a, index, text) || text.size() > 4 * 1024 * 1024)
    {
        return false;
    }
    out = Json::parse(text, nullptr, false);
    return !out.is_discarded();
}
template <class T> void get_settings(const Arguments& a)
{
    auto c = binding_detail::component<T>(a, 2);
    a.set_return({});
    typename T::Data data;
    if (c && c.get_settings(data))
    {
        a.set_return(native(schema::control_json::write(data, true)));
    }
}
template <class T> void set_settings(const Arguments& a)
{
    auto c = binding_detail::component<T>(a, 3);
    Json j;
    typename T::Data data;
    std::string error;
    if (!c || !argument(a, 2, j) || !schema::control_json::read(j, data, error, true))
    {
        return;
    }
    if constexpr (requires { c.set_settings(data, error); })
    {
        a.set_return(c.set_settings(data, error));
    }
    else
    {
        a.set_return(c.set_settings(data));
    }
}
} // namespace engine::plugins::script::bindings::control_detail
