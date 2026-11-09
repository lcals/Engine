#pragma once
#include "runtime/schema/control_json_generated.hpp"
#include <sstream>
#include <toml++/toml.hpp>
namespace engine::editor::asset_pipeline::control_toml
{
using Json = nlohmann::json;
inline bool read(const toml::table& table, Json& output, std::string& error)
{
    std::ostringstream stream;
    stream << toml::json_formatter(table);
    output = Json::parse(stream.str(), nullptr, false);
    if (output.is_discarded())
    {
        error = "Invalid control TOML";
        return false;
    }
    output.erase("type");
    output.erase("enabled");
    output.erase("visible");
    return true;
}
inline void write_table(const Json& value, toml::table& output);
inline void write_array(const Json& value, toml::array& output)
{
    for (const auto& v : value)
    {
        if (v.is_object())
        {
            toml::table t;
            write_table(v, t);
            output.push_back(std::move(t));
        }
        else if (v.is_array())
        {
            toml::array a;
            write_array(v, a);
            output.push_back(std::move(a));
        }
        else if (v.is_boolean())
        {
            output.push_back(v.get<bool>());
        }
        else if (v.is_string())
        {
            output.push_back(v.get<std::string>());
        }
        else if (v.is_number_integer())
        {
            output.push_back(v.get<std::int64_t>());
        }
        else if (v.is_number())
        {
            output.push_back(v.get<double>());
        }
    }
}
inline void write_table(const Json& value, toml::table& output)
{
    for (auto i = value.begin(); i != value.end(); ++i)
    {
        const auto& v = i.value();
        const auto& k = i.key();
        if (v.is_object())
        {
            toml::table t;
            write_table(v, t);
            output.insert_or_assign(k, std::move(t));
        }
        else if (v.is_array())
        {
            toml::array a;
            write_array(v, a);
            output.insert_or_assign(k, std::move(a));
        }
        else if (v.is_boolean())
        {
            output.insert_or_assign(k, v.get<bool>());
        }
        else if (v.is_string())
        {
            output.insert_or_assign(k, v.get<std::string>());
        }
        else if (v.is_number_integer())
        {
            output.insert_or_assign(k, v.get<std::int64_t>());
        }
        else if (v.is_number())
        {
            output.insert_or_assign(k, v.get<double>());
        }
    }
}
} // namespace engine::editor::asset_pipeline::control_toml
