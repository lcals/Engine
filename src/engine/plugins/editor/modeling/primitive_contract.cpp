#include "plugins/editor/modeling/primitive_contract.hpp"

#include "plugins/editor/modeling/primitive_options.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace engine::plugins::modeling::detail
{
namespace
{
using Json = nlohmann::json;

Json value(const schema::ModelingPrimitiveDataT& options, const PrimitiveParameter& parameter)
{
    if (parameter.scalar)
    {
        return options.*parameter.scalar;
    }
    if (parameter.count)
    {
        return options.*parameter.count;
    }
    const std::array size{options.size.x(), options.size.y(), options.size.z()};
    return parameter.size_axis == 3 ? Json(size) : Json(size[parameter.size_axis]);
}

Json parameter_schema(const PrimitiveParameter& parameter)
{
    Json numeric = {
        {"type", parameter.count ? "integer" : "number"},
        {parameter.exclusive_minimum ? "exclusiveMinimum" : "minimum", parameter.minimum},
        {"maximum", parameter.maximum}};
    if (parameter.name == "angleDegrees")
    {
        numeric.erase("maximum");
        numeric["exclusiveMaximum"] = parameter.maximum;
    }
    if (parameter.size_axis == 3)
    {
        numeric = {{"type", "array"}, {"items", numeric}, {"minItems", 3}, {"maxItems", 3}};
    }
    numeric["default"] = value(schema::ModelingPrimitiveDataT{}, parameter);
    return numeric;
}

Json object(Json properties, Json required = Json::array())
{
    return {{"type", "object"},
            {"properties", std::move(properties)},
            {"required", std::move(required)},
            {"additionalProperties", false}};
}

Json points(std::size_t dimensions, std::size_t minimum)
{
    return {{"type", "array"},
            {"minItems", minimum},
            {"maxItems", 65536},
            {"items",
             {{"type", "array"},
              {"minItems", dimensions},
              {"maxItems", dimensions},
              {"items", {{"type", "number"}}}}}};
}
} // namespace

bool patch_primitive(std::string_view shape, const Json& parameters,
                     schema::ModelingPrimitiveDataT& options, std::string& error)
{
    error.clear();
    const auto& types = schema::EnumValuesModelingPrimitive();
    const auto type = std::find_if(std::begin(types), std::end(types), [&](auto item)
                                   { return shape == schema::EnumNameModelingPrimitive(item); });
    if (type == std::end(types) || !parameters.is_object())
    {
        error = "Unknown primitive shape or non-object parameters";
        return false;
    }
    auto candidate = options;
    candidate.type = *type;
    std::array size{candidate.size.x(), candidate.size.y(), candidate.size.z()};
    for (const auto& [name, input] : parameters.items())
    {
        const auto definitions = primitive_parameters();
        const auto field =
            std::find_if(definitions.begin(), definitions.end(), [&](const auto& item)
                         { return item.name == name && supports(item, *type); });
        if (field == definitions.end())
        {
            error = "Parameter is not supported by " + std::string(shape) + ": " + name;
            return false;
        }
        if (field->size_axis == 3)
        {
            if (!input.is_array() || input.size() != 3)
            {
                error = "size requires three finite positive numbers";
                return false;
            }
            for (std::size_t axis = 0; axis < size.size(); ++axis)
            {
                if (!input[axis].is_number() || !std::isfinite(input[axis].get<double>()))
                {
                    error = "size requires three finite positive numbers";
                    return false;
                }
                size[axis] = input[axis].get<double>();
            }
        }
        else
        {
            if (!input.is_number() || !std::isfinite(input.get<double>()) ||
                input.get<double>() < field->minimum || input.get<double>() > field->maximum ||
                (field->count && !input.is_number_integer()))
            {
                error = "Invalid primitive parameter: " + name;
                return false;
            }
            if (field->scalar)
            {
                candidate.*field->scalar = input.get<double>();
            }
            else if (field->count)
            {
                candidate.*field->count = input.get<std::uint32_t>();
            }
            else
            {
                size[field->size_axis] = input.get<double>();
            }
        }
    }
    candidate.size = {size[0], size[1], size[2]};
    if (!validate_primitive(candidate, error))
    {
        return false;
    }
    options = std::move(candidate);
    return true;
}

Json primitive_json(const schema::ModelingPrimitiveDataT& options)
{
    Json parameters = Json::object();
    for (const auto& parameter : primitive_parameters())
    {
        if (supports(parameter, options.type))
        {
            parameters[parameter.name] = value(options, parameter);
        }
    }
    return {{"shape", schema::EnumNameModelingPrimitive(options.type)},
            {"parameters", std::move(parameters)}};
}

Json shape_contracts(bool primitives_only)
{
    Json contracts = Json::object();
    for (auto type : schema::EnumValuesModelingPrimitive())
    {
        schema::ModelingPrimitiveDataT defaults;
        defaults.type = type;
        Json properties = Json::object();
        for (const auto& parameter : primitive_parameters())
        {
            if (supports(parameter, type))
            {
                properties[parameter.name] = parameter_schema(parameter);
            }
        }
        const auto name = schema::EnumNameModelingPrimitive(type);
        contracts[name] = {
            {"parameters", object(std::move(properties))},
            {"example", primitive_json(defaults)},
            {"notes", "UV tiles are per model-local unit; node scale does not rewrite UV."}};
    }
    contracts["stairs"]["notes"] = "size: X run length, Y total height, Z width; steps in [1,256].";
    contracts["door"]["notes"] = "thickness < half width and height.";
    contracts["torus"]["notes"] = "tubeRadius < radius.";
    contracts["arch"]["notes"] = "XY arc, Z depth; innerRadius < radius; 0 < angleDegrees < 360.";
    contracts["pipe"]["notes"] = "Y height; innerRadius < radius.";
    contracts["curved_stairs"]["notes"] =
        "Y height; innerRadius < radius; angleDegrees < 360 and angleDegrees / steps < 180.";
    if (!primitives_only)
    {
        contracts["polygon"] = {
            {"parameters",
             object({{"points", points(3, 3)},
                     {"holes", {{"type", "array"}, {"items", points(3, 3)}, {"maxItems", 1024}}}},
                    {"points"})},
            {"example",
             {{"shape", "polygon"},
              {"parameters",
               {{"points", {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}}, {"holes", Json::array()}}}}}};
        contracts["sweep"] = {
            {"parameters", object({{"profile", points(2, 2)},
                                   {"path", points(3, 2)},
                                   {"closedProfile", {{"type", "boolean"}, {"default", true}}},
                                   {"cap", {{"type", "boolean"}, {"default", true}}},
                                   {"twistDegrees", {{"type", "number"}, {"default", 0}}}},
                                  {"profile", "path"})},
            {"example",
             {{"shape", "sweep"},
              {"parameters",
               {{"profile", {{-0.5, -0.5}, {0.5, -0.5}, {0.5, 0.5}, {-0.5, 0.5}}},
                {"path", {{0, 0, 0}, {0, 0, 2}}}}}}}};
        contracts["lathe"] = {
            {"parameters",
             object({{"profile", points(2, 2)},
                     {"segments",
                      {{"type", "integer"}, {"minimum", 3}, {"maximum", 16384}, {"default", 32}}},
                     {"angleDegrees",
                      {{"type", "number"},
                       {"exclusiveMinimum", 0},
                       {"maximum", 360},
                       {"default", 360}}},
                     {"cap", {{"type", "boolean"}, {"default", true}}}},
                    {"profile"})},
            {"example",
             {{"shape", "lathe"}, {"parameters", {{"profile", {{0.5, -0.5}, {0.5, 0.5}}}}}}}};
    }
    return contracts;
}
} // namespace engine::plugins::modeling::detail
