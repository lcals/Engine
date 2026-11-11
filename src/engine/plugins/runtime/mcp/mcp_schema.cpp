#include "plugins/runtime/mcp/mcp_schema.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace engine::plugins::mcp::protocol
{
namespace
{
using Json = nlohmann::json;

bool typed_value(std::string_view type, const Json& value)
{
    if (type == "object")
    {
        return value.is_object();
    }
    if (type == "array")
    {
        return value.is_array();
    }
    if (type == "string")
    {
        return value.is_string();
    }
    if (type == "boolean")
    {
        return value.is_boolean();
    }
    if (type == "integer")
    {
        return value.is_number_integer();
    }
    if (type == "number")
    {
        return value.is_number() && std::isfinite(value.get<double>());
    }
    return type == "null" && value.is_null();
}

bool allows_type(const Json& type, std::string_view name)
{
    return type.is_string() ? type == name
                            : std::find(type.begin(), type.end(), name) != type.end();
}

bool typed_value(const Json& type, const Json& value)
{
    if (type.is_string())
    {
        return typed_value(std::string_view(type.get_ref<const std::string&>()), value);
    }
    return std::any_of(type.begin(), type.end(),
                       [&](const auto& item)
                       {
                           return typed_value(
                               std::string_view(item.template get_ref<const std::string&>()),
                               value);
                       });
}

bool inspect_schema(const Json& schema, std::size_t depth)
{
    if (schema.is_object() && schema.empty())
    {
        return true;
    }
    if (depth > 16 || !schema.is_object() || !schema.contains("type"))
    {
        return false;
    }
    const auto& type = schema["type"];
    const auto known = [](const Json& item)
    {
        return item.is_string() &&
               (item == "object" || item == "array" || item == "string" || item == "boolean" ||
                item == "integer" || item == "number" || item == "null");
    };
    if (!(known(type) ||
          (type.is_array() && !type.empty() && std::all_of(type.begin(), type.end(), known))))
    {
        return false;
    }
    for (const auto& [key, value] : schema.items())
    {
        if (key == "type")
        {
            continue;
        }
        if (key == "description")
        {
            if (!value.is_string())
            {
                return false;
            }
        }
        else if (key == "default")
        {
            if (!typed_value(type, value))
            {
                return false;
            }
        }
        else if (key == "oneOf")
        {
            if (!value.is_array() || value.empty() ||
                !std::all_of(value.begin(), value.end(), [depth](const auto& branch)
                             { return inspect_schema(branch, depth + 1); }))
            {
                return false;
            }
        }
        else if (key == "enum")
        {
            if (!value.is_array() || value.empty() ||
                !std::all_of(value.begin(), value.end(),
                             [&type](const auto& item) { return typed_value(type, item); }))
            {
                return false;
            }
        }
        else if (key == "properties" && allows_type(type, "object"))
        {
            if (!value.is_object())
            {
                return false;
            }
            for (const auto& property : value)
            {
                if (!inspect_schema(property, depth + 1))
                {
                    return false;
                }
            }
        }
        else if (key == "required" && allows_type(type, "object"))
        {
            if (!value.is_array() || !schema.contains("properties") ||
                !schema["properties"].is_object())
            {
                return false;
            }
            for (const auto& name : value)
            {
                if (!name.is_string() || !schema["properties"].contains(name.get<std::string>()))
                {
                    return false;
                }
            }
        }
        else if (key == "additionalProperties" && allows_type(type, "object"))
        {
            if (!value.is_boolean() && !inspect_schema(value, depth + 1))
            {
                return false;
            }
        }
        else if (key == "items" && allows_type(type, "array"))
        {
            if (!inspect_schema(value, depth + 1))
            {
                return false;
            }
        }
        else if (((key == "minItems" || key == "maxItems") && allows_type(type, "array")) ||
                 ((key == "minLength" || key == "maxLength") && allows_type(type, "string")))
        {
            if (!value.is_number_integer() || value < 0)
            {
                return false;
            }
        }
        else if ((key == "minimum" || key == "maximum" || key == "exclusiveMinimum" ||
                  key == "exclusiveMaximum") &&
                 (allows_type(type, "integer") || allows_type(type, "number")))
        {
            if (!value.is_number() || !std::isfinite(value.get<double>()))
            {
                return false;
            }
        }
        else
        {
            return false;
        }
    }
    for (const auto& [minimum, maximum] :
         {std::pair{"minimum", "maximum"}, {"minItems", "maxItems"}, {"minLength", "maxLength"}})
    {
        if (schema.contains(minimum) && schema.contains(maximum) &&
            schema[minimum] > schema[maximum])
        {
            return false;
        }
    }
    return true;
}

bool matches(const Json& schema, const Json& value)
{
    if (schema.empty())
    {
        return true;
    }
    const auto& type = schema["type"];
    if (!typed_value(type, value))
    {
        return false;
    }
    if (schema.contains("enum") &&
        std::find(schema["enum"].begin(), schema["enum"].end(), value) == schema["enum"].end())
    {
        return false;
    }
    if (schema.contains("oneOf") &&
        std::count_if(schema["oneOf"].begin(), schema["oneOf"].end(),
                      [&value](const auto& branch) { return matches(branch, value); }) != 1)
    {
        return false;
    }
    if (value.is_object())
    {
        if (schema.contains("required"))
        {
            for (const auto& required : schema["required"])
            {
                if (!value.contains(required.get_ref<const std::string&>()))
                {
                    return false;
                }
            }
        }
        for (const auto& [name, argument] : value.items())
        {
            if (schema.contains("properties") && schema["properties"].contains(name))
            {
                if (!matches(schema["properties"][name], argument))
                {
                    return false;
                }
            }
            else if (schema.contains("additionalProperties"))
            {
                const auto& extra = schema["additionalProperties"];
                if (extra.is_boolean() ? !extra.get<bool>() : !matches(extra, argument))
                {
                    return false;
                }
            }
        }
    }
    else if (value.is_array())
    {
        if ((schema.contains("minItems") && value.size() < schema["minItems"].get<std::size_t>()) ||
            (schema.contains("maxItems") && value.size() > schema["maxItems"].get<std::size_t>()))
        {
            return false;
        }
        if (schema.contains("items"))
        {
            for (const auto& item : value)
            {
                if (!matches(schema["items"], item))
                {
                    return false;
                }
            }
        }
    }
    else if (value.is_string())
    {
        const auto& text = value.get_ref<const std::string&>();
        const auto length = static_cast<std::size_t>(std::count_if(
            text.begin(), text.end(), [](unsigned char byte) { return (byte & 0xc0) != 0x80; }));
        if ((schema.contains("minLength") && length < schema["minLength"].get<std::size_t>()) ||
            (schema.contains("maxLength") && length > schema["maxLength"].get<std::size_t>()))
        {
            return false;
        }
    }
    else if (value.is_number())
    {
        if ((schema.contains("minimum") && value < schema["minimum"]) ||
            (schema.contains("maximum") && value > schema["maximum"]) ||
            (schema.contains("exclusiveMinimum") && value <= schema["exclusiveMinimum"]) ||
            (schema.contains("exclusiveMaximum") && value >= schema["exclusiveMaximum"]))
        {
            return false;
        }
    }
    return true;
}
} // namespace

bool valid_schema(const Json& schema)
{
    return inspect_schema(schema, 0) && schema.contains("type") && schema["type"] == "object";
}

bool valid_arguments(const Json& schema, const Json& arguments)
{
    return matches(schema, arguments);
}

} // namespace engine::plugins::mcp::protocol
