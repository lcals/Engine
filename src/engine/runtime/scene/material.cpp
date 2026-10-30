#include "runtime/scene/material.hpp"

#include "runtime/resource/resource_system.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace engine
{
namespace
{

const schema::ShaderParameterT* find_definition(const ResourcePtr& resource, std::string_view name,
                                                schema::MaterialValue value_type)
{
    const schema::ShaderDataT* shader = resource ? resource->shader() : nullptr;
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

bool valid_values(const ResourcePtr& resource, std::string_view name,
                  schema::MaterialValue value_type, std::initializer_list<double> values)
{
    const auto* definition = find_definition(resource, name, value_type);
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

schema::MaterialDataT shader_defaults(std::string_view type_path, const Resource& resource)
{
    const auto& shader = *resource.shader();
    schema::MaterialDataT result;
    result.type_path = type_path;
    result.shader_path = shader.path;
    result.shader_guid = resource.info().guid;
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

const schema::ShaderSamplerT* find_sampler(const ResourcePtr& shader, std::string_view name)
{
    if (!shader || !shader->shader())
    {
        return nullptr;
    }
    for (const auto& sampler : shader->shader()->samplers)
    {
        if (sampler->name == name)
        {
            return sampler.get();
        }
    }
    return nullptr;
}

bool load_textures(const Resource& shader, const schema::MaterialDataT* material,
                   schema::MaterialDataT& result,
                   std::map<std::string, ResourcePtr, std::less<>>& textures)
{
    auto* resources = ResourceSystem::instance();
    if (!resources)
    {
        return false;
    }
    for (const auto& sampler : shader.shader()->samplers)
    {
        std::string path = sampler->default_texture;
        if (material && sampler->exposed)
        {
            for (const auto& binding : material->textures)
            {
                if (binding->name == sampler->name)
                {
                    path = binding->texture_path;
                    break;
                }
            }
        }
        Guid guid;
        ResourcePtr texture;
        if (!ResourceSystem::path_to_guid(path, guid) || !resources->loader().load(guid, texture) ||
            !texture->texture())
        {
            return false;
        }
        auto binding = std::make_unique<schema::MaterialTextureT>();
        binding->name = sampler->name;
        binding->texture_guid = guid;
        binding->texture_path = path;
        result.textures.push_back(std::move(binding));
        textures.emplace(sampler->name, std::move(texture));
    }
    return true;
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

const schema::ShaderDataT* Material::shader_data() const noexcept
{
    return shader_resource_ ? shader_resource_->shader() : nullptr;
}

bool Material::set_type(std::string_view path)
{
    if (data_.type_path == path && material_resource_ && shader_resource_)
    {
        return true;
    }
    auto* resources = ResourceSystem::instance();
    Guid guid;
    ResourcePtr material;
    if (resources == nullptr || !ResourceSystem::path_to_guid(path, guid) ||
        !resources->loader().load(guid, material) || material->material() == nullptr)
    {
        return false;
    }
    const auto* definition = material->material();
    ResourcePtr shader;
    if (!ResourceSystem::path_to_guid(definition->shader_path, guid) ||
        !resources->loader().load(guid, shader) || shader->shader() == nullptr)
    {
        return false;
    }
    auto replacement = shader_defaults(definition->type_path, *shader);
    replacement.cull_mode = definition->cull_mode;
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
    std::map<std::string, ResourcePtr, std::less<>> textures;
    if (!load_textures(*shader, definition, replacement, textures))
    {
        return false;
    }
    std::vector<ResourcePtr> dependencies;
    dependencies.reserve(material->info().dependencies.size());
    for (const auto& dependency : material->info().dependencies)
    {
        if (dependency == shader->info().guid)
        {
            dependencies.push_back(shader);
            continue;
        }
        const auto texture =
            std::find_if(textures.begin(), textures.end(), [&dependency](const auto& entry)
                         { return entry.second->info().guid == dependency; });
        if (texture == textures.end())
        {
            return false;
        }
        dependencies.push_back(texture->second);
    }
    if (!resources->validate_dependencies(*material, dependencies))
    {
        return false;
    }
    data_ = std::move(replacement);
    textures_ = std::move(textures);
    material_resource_ = std::move(material);
    shader_resource_ = std::move(shader);
    return true;
}

bool Material::set_shader(std::string_view path)
{
    if (!material_resource_)
    {
        return false;
    }
    if (data_.shader_path == path && shader_resource_)
    {
        return true;
    }
    auto* resources = ResourceSystem::instance();
    Guid guid;
    ResourcePtr shader;
    if (resources == nullptr || !ResourceSystem::path_to_guid(path, guid) ||
        !resources->loader().load(guid, shader) || shader->shader() == nullptr)
    {
        return false;
    }
    auto replacement = shader_defaults(data_.type_path, *shader);
    replacement.cull_mode = data_.cull_mode;
    std::map<std::string, ResourcePtr, std::less<>> textures;
    if (!load_textures(*shader, nullptr, replacement, textures))
    {
        return false;
    }
    data_ = std::move(replacement);
    textures_ = std::move(textures);
    shader_resource_ = std::move(shader);
    return true;
}

schema::CullMode Material::cull_mode() const noexcept
{
    return data_.cull_mode;
}

bool Material::set_cull_mode(schema::CullMode mode) noexcept
{
    if (mode != schema::CullMode::back && mode != schema::CullMode::front &&
        mode != schema::CullMode::none)
    {
        return false;
    }
    data_.cull_mode = mode;
    return true;
}

bool Material::set_texture(std::string_view name, std::string_view path)
{
    const auto* sampler = find_sampler(shader_resource_, name);
    auto* resources = ResourceSystem::instance();
    Guid guid;
    ResourcePtr replacement;
    if (!sampler || !sampler->exposed || !resources || !ResourceSystem::path_to_guid(path, guid) ||
        !resources->loader().load(guid, replacement) || !replacement->texture())
    {
        return false;
    }
    const auto current = textures_.find(name);
    const auto binding = std::find_if(data_.textures.begin(), data_.textures.end(),
                                      [name](const auto& item) { return item->name == name; });
    if (current == textures_.end() || binding == data_.textures.end())
    {
        return false;
    }
    (*binding)->texture_guid = guid;
    (*binding)->texture_path = path;
    current->second = std::move(replacement);
    return true;
}

bool Material::get_texture(std::string_view name, std::string_view& output) const noexcept
{
    output = {};
    const auto* sampler = find_sampler(shader_resource_, name);
    const auto& resource = texture(name);
    if (!sampler || !sampler->exposed || !resource)
    {
        return false;
    }
    output = resource->info().path;
    return true;
}

const ResourcePtr& Material::texture(std::string_view name) const noexcept
{
    static const ResourcePtr empty;
    const auto found = textures_.find(name);
    return found == textures_.end() ? empty : found->second;
}

bool Material::set_float(std::string_view name, float value) noexcept
{
    auto* parameter = find_parameter(name, schema::MaterialValue::FloatValue);
    if (parameter == nullptr ||
        !valid_values(shader_resource_, name, schema::MaterialValue::FloatValue, {value}))
    {
        return false;
    }
    parameter->value.AsFloatValue()->value = value;
    return true;
}

bool Material::set_v2(std::string_view name, const Vec2& value) noexcept
{
    auto* parameter = find_parameter(name, schema::MaterialValue::Vec2Value);
    if (parameter == nullptr ||
        !valid_values(shader_resource_, name, schema::MaterialValue::Vec2Value,
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
    if (parameter == nullptr ||
        !valid_values(shader_resource_, name, schema::MaterialValue::Vec3Value,
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
    if (parameter == nullptr ||
        !valid_values(shader_resource_, name, schema::MaterialValue::Vec4Value,
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
    if (!valid_values(shader_resource_, name, schema::MaterialValue::FloatValue, {value}))
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
    if (!valid_values(shader_resource_, name, schema::MaterialValue::Vec2Value,
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
    if (!valid_values(shader_resource_, name, schema::MaterialValue::Vec3Value,
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
    if (!valid_values(shader_resource_, name, schema::MaterialValue::Vec4Value,
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
    if (find_definition(shader_resource_, name, type) == nullptr)
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

void Material::swap(Material& other) noexcept
{
    using std::swap;
    swap(material_resource_, other.material_resource_);
    swap(shader_resource_, other.shader_resource_);
    swap(data_, other.data_);
    swap(textures_, other.textures_);
}

} // namespace engine
