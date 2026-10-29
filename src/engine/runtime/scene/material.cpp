#include "runtime/scene/material.hpp"

#include "runtime/resource/resource_system.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <memory>
#include <utility>

namespace engine
{
namespace
{

const schema::ShaderParameterT* find_definition(std::string_view shader_path, std::string_view name,
                                                schema::MaterialValue value_type)
{
    const schema::ShaderDataT* shader = ResourceSystem::find_shader(shader_path);
    if (shader == nullptr)
    {
        return nullptr;
    }
    const auto iterator = std::find_if(shader->parameters.begin(), shader->parameters.end(),
                                       [name, value_type](const auto& parameter)
                                       {
                                           return parameter->exposed && parameter->name == name &&
                                                  parameter->default_value.type == value_type;
                                       });
    return iterator == shader->parameters.end() ? nullptr : iterator->get();
}

bool valid_values(std::string_view shader_path, std::string_view name,
                  schema::MaterialValue value_type, std::initializer_list<double> values)
{
    const auto* definition = find_definition(shader_path, name, value_type);
    return definition != nullptr &&
           std::all_of(values.begin(), values.end(),
                       [definition](double value)
                       {
                           constexpr double maximum = std::numeric_limits<float>::max();
                           return std::isfinite(value) && value >= -maximum && value <= maximum &&
                                  (!definition->bounded ||
                                   (value >= definition->minimum && value <= definition->maximum));
                       });
}

schema::MaterialDataT shader_defaults(std::string_view type_path, const schema::ShaderDataT& shader)
{
    schema::MaterialDataT result;
    result.type_path = type_path;
    result.shader_path = shader.path;
    (void)ResourceSystem::path_to_guid(shader.path, result.shader_guid);
    for (const auto& definition : shader.parameters)
    {
        if (!definition->exposed)
        {
            continue;
        }
        auto parameter = std::make_unique<schema::MaterialParameterT>();
        parameter->name = definition->name;
        parameter->value = definition->default_value;
        result.parameters.push_back(std::move(parameter));
    }
    return result;
}

} // namespace

Material::Material() noexcept
{
}

std::string_view Material::type_path() const noexcept
{
    return data_.type_path;
}

std::string_view Material::shader_path() const noexcept
{
    return data_.shader_path;
}

bool Material::set_type(std::string_view path)
{
    const schema::MaterialDataT* definition = ResourceSystem::find_material(path);
    if (definition == nullptr)
    {
        return false;
    }
    if (data_.type_path == path)
    {
        return true;
    }
    const schema::ShaderDataT* shader = ResourceSystem::find_shader(definition->shader_path);
    if (shader == nullptr)
    {
        return false;
    }
    auto replacement = shader_defaults(definition->type_path, *shader);
    for (const auto& parameter : definition->parameters)
    {
        const auto destination =
            std::find_if(replacement.parameters.begin(), replacement.parameters.end(),
                         [&parameter](const auto& item) { return item->name == parameter->name; });
        if (destination == replacement.parameters.end())
        {
            return false;
        }
        (*destination)->value = parameter->value;
    }
    data_ = std::move(replacement);
    return true;
}

bool Material::set_shader(std::string_view path)
{
    const schema::ShaderDataT* shader = ResourceSystem::find_shader(path);
    if (shader == nullptr || ResourceSystem::find_material(data_.type_path) == nullptr)
    {
        return false;
    }
    if (data_.shader_path == path)
    {
        return true;
    }
    auto replacement = shader_defaults(data_.type_path, *shader);
    data_ = std::move(replacement);
    return true;
}

bool Material::set_float(std::string_view name, float value) noexcept
{
    auto* parameter = find_parameter(name, schema::MaterialValue::FloatValue);
    if (parameter == nullptr ||
        !valid_values(shader_path(), name, schema::MaterialValue::FloatValue, {value}))
    {
        return false;
    }
    parameter->value.AsFloatValue()->value = value;
    return true;
}

bool Material::set_v2(std::string_view name, const Vec2& value) noexcept
{
    auto* parameter = find_parameter(name, schema::MaterialValue::Vec2Value);
    if (parameter == nullptr || !valid_values(shader_path(), name, schema::MaterialValue::Vec2Value,
                                              {value.x(), value.y()}))
    {
        return false;
    }
    parameter->value.AsVec2Value()->value = value;
    return true;
}

bool Material::set_v3(std::string_view name, const Vec3& value) noexcept
{
    auto* parameter = find_parameter(name, schema::MaterialValue::Vec3Value);
    if (parameter == nullptr || !valid_values(shader_path(), name, schema::MaterialValue::Vec3Value,
                                              {value.x(), value.y(), value.z()}))
    {
        return false;
    }
    parameter->value.AsVec3Value()->value = value;
    return true;
}

bool Material::set_v4(std::string_view name, const Vec4& value) noexcept
{
    auto* parameter = find_parameter(name, schema::MaterialValue::Vec4Value);
    if (parameter == nullptr || !valid_values(shader_path(), name, schema::MaterialValue::Vec4Value,
                                              {value.x(), value.y(), value.z(), value.w()}))
    {
        return false;
    }
    parameter->value.AsVec4Value()->value = value;
    return true;
}

bool Material::get_float(std::string_view name, float& output) const noexcept
{
    output = 0.0F;
    const auto* parameter = find_parameter(name, schema::MaterialValue::FloatValue);
    if (parameter == nullptr)
    {
        return false;
    }
    const float value = parameter->value.AsFloatValue()->value;
    if (!valid_values(shader_path(), name, schema::MaterialValue::FloatValue, {value}))
    {
        return false;
    }
    output = value;
    return true;
}

bool Material::get_v2(std::string_view name, Vec2& output) const noexcept
{
    output = {};
    const auto* parameter = find_parameter(name, schema::MaterialValue::Vec2Value);
    if (parameter == nullptr)
    {
        return false;
    }
    const Vec2& value = parameter->value.AsVec2Value()->value;
    if (!valid_values(shader_path(), name, schema::MaterialValue::Vec2Value,
                      {value.x(), value.y()}))
    {
        return false;
    }
    output = value;
    return true;
}

bool Material::get_v3(std::string_view name, Vec3& output) const noexcept
{
    output = {};
    const auto* parameter = find_parameter(name, schema::MaterialValue::Vec3Value);
    if (parameter == nullptr)
    {
        return false;
    }
    const Vec3& value = parameter->value.AsVec3Value()->value;
    if (!valid_values(shader_path(), name, schema::MaterialValue::Vec3Value,
                      {value.x(), value.y(), value.z()}))
    {
        return false;
    }
    output = value;
    return true;
}

bool Material::get_v4(std::string_view name, Vec4& output) const noexcept
{
    output = {};
    const auto* parameter = find_parameter(name, schema::MaterialValue::Vec4Value);
    if (parameter == nullptr)
    {
        return false;
    }
    const Vec4& value = parameter->value.AsVec4Value()->value;
    if (!valid_values(shader_path(), name, schema::MaterialValue::Vec4Value,
                      {value.x(), value.y(), value.z(), value.w()}))
    {
        return false;
    }
    output = value;
    return true;
}

schema::MaterialParameterT* Material::find_parameter(std::string_view name,
                                                     schema::MaterialValue type) noexcept
{
    return const_cast<schema::MaterialParameterT*>(std::as_const(*this).find_parameter(name, type));
}

const schema::MaterialParameterT*
Material::find_parameter(std::string_view name, schema::MaterialValue type) const noexcept
{
    if (find_definition(data_.shader_path, name, type) == nullptr)
    {
        return nullptr;
    }
    const auto iterator = std::find_if(data_.parameters.begin(), data_.parameters.end(),
                                       [name, type](const auto& parameter)
                                       {
                                           return parameter != nullptr && parameter->name == name &&
                                                  parameter->value.type == type &&
                                                  parameter->value.value != nullptr;
                                       });
    return iterator == data_.parameters.end() ? nullptr : iterator->get();
}

} // namespace engine
