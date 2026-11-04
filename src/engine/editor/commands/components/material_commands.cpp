#include "editor/commands/components/component_edit.hpp"

#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/material.hpp"
#include "runtime/scene/mesh_renderer.hpp"

#include <algorithm>

namespace engine::editor::components
{
namespace
{
bool read_parameter(const Material& material, std::string_view name, std::string_view type,
                    std::array<double, 4>& output)
{
    output = {};
    if (type == "float")
    {
        float value = 0;
        if (!material.get_float(name, value))
        {
            return false;
        }
        output[0] = value;
        return true;
    }
    if (type == "v2")
    {
        Vec2 value;
        if (!material.get_v2(name, value))
        {
            return false;
        }
        output = {value.x(), value.y(), 0, 0};
        return true;
    }
    if (type == "v3")
    {
        Vec3 value;
        if (!material.get_v3(name, value))
        {
            return false;
        }
        output = {value.x(), value.y(), value.z(), 0};
        return true;
    }
    if (type == "v4")
    {
        Vec4 value;
        if (!material.get_v4(name, value))
        {
            return false;
        }
        output = {value.x(), value.y(), value.z(), value.w()};
        return true;
    }
    return false;
}

class MaterialEdit final : public EditorEdit
{
public:
    Target target;
    std::size_t slot = 0;
    std::string name;
    std::string type;
    std::string material_path;
    std::string shader_path;
    std::array<double, 4> before{};
    std::array<double, 4> after{};

    explicit MaterialEdit(Target value) : target(value)
    {
    }

    bool apply(std::string& error) override
    {
        return change(before, after, error);
    }

    bool revert(std::string& error) override
    {
        return change(after, before, error);
    }

private:
    bool change(const std::array<double, 4>& expected, const std::array<double, 4>& replacement,
                std::string& error)
    {
        auto node = target.node(error);
        auto component = node.valid() ? node.get_component<MeshRenderer>() : MeshRenderer{};
        if (!component)
        {
            error = "Material target no longer exists";
            return false;
        }
        auto selected = component.material(slot);
        if (!selected)
        {
            error = "Material slot no longer exists";
            return false;
        }
        auto material = selected;
        std::array<double, 4> current{};
        if (material.type_path() != material_path || material.shader_path() != shader_path ||
            !read_parameter(material, name, type, current) || current != expected)
        {
            error = "Material changed outside this edit; undo or redo is stale";
            return false;
        }
        bool success = false;
        if (type == "float")
        {
            success = material.set_float(name, static_cast<float>(replacement[0]));
        }
        else if (type == "v2")
        {
            success = material.set_v2(name, {replacement[0], replacement[1]});
        }
        else if (type == "v3")
        {
            success = material.set_v3(name, {replacement[0], replacement[1], replacement[2]});
        }
        else if (type == "v4")
        {
            success = material.set_v4(
                name, {replacement[0], replacement[1], replacement[2], replacement[3]});
        }
        if (!success)
        {
            error = "Invalid material parameter type or value";
        }
        return success;
    }
};

bool read_slot(const Json& arguments, std::size_t& output)
{
    output = 0;
    if (!arguments.contains("slot"))
    {
        return true;
    }
    const auto& value = arguments["slot"];
    if (!value.is_number_integer() || value < 0 || value >= 16)
    {
        return false;
    }
    output = value.get<std::size_t>();
    return true;
}

class TextureEdit final : public EditorEdit
{
public:
    explicit TextureEdit(Target value) : target(value)
    {
    }
    Target target;
    std::size_t slot = 0;
    std::string name;
    std::string material_path;
    std::string shader_path;
    schema::AssetEntryT before;
    schema::AssetEntryT after;
    bool apply(std::string& error) override
    {
        return change(before, after, error);
    }
    bool revert(std::string& error) override
    {
        return change(after, before, error);
    }

private:
    bool change(const schema::AssetEntryT& expected, const schema::AssetEntryT& replacement,
                std::string& error)
    {
        auto node = target.node(error);
        auto renderer = node ? node.get_component<MeshRenderer>() : MeshRenderer{};
        auto material = renderer ? renderer.material(slot) : Material{};
        auto* resources = ResourceSystem::instance();
        const auto* available = resources ? resources->catalog().find(replacement.guid) : nullptr;
        const auto current = material ? material.texture(name) : ResourcePtr{};
        if (!current || current->info() != expected || !available || *available != replacement ||
            !material || material.type_path() != material_path ||
            material.shader_path() != shader_path || !current->texture())
        {
            error = "Texture target changed; undo or redo is stale";
            return false;
        }
        if (!material.set_texture(name, replacement.path))
        {
            error = "Texture assignment failed";
            return false;
        }
        return true;
    }
};

Json material_json(const Material& material)
{
    Json parameters = Json::array();
    const auto* shader = material.shader_data();
    if (shader != nullptr)
    {
        for (const auto& parameter : shader->parameters)
        {
            if (!parameter->exposed)
            {
                continue;
            }
            std::string type;
            std::size_t count = 0;
            switch (parameter->default_value.type)
            {
            case schema::MaterialValue::FloatValue:
                type = "float";
                count = 1;
                break;
            case schema::MaterialValue::Vec2Value:
                type = "v2";
                count = 2;
                break;
            case schema::MaterialValue::Vec3Value:
                type = "v3";
                count = 3;
                break;
            case schema::MaterialValue::Vec4Value:
                type = "v4";
                count = 4;
                break;
            default:
                continue;
            }
            std::array<double, 4> values{};
            if (!read_parameter(material, parameter->name, type, values))
            {
                continue;
            }
            Json value = values[0];
            if (count != 1)
            {
                value = Json::array();
                for (std::size_t index = 0; index < count; ++index)
                {
                    value.push_back(values[index]);
                }
            }
            parameters.push_back({{"name", parameter->name}, {"type", type}, {"value", value}});
        }
    }
    Json textures = Json::array();
    if (shader)
    {
        for (const auto& sampler : shader->samplers)
        {
            std::string_view path;
            if (material.get_texture(sampler->name, path))
            {
                textures.push_back({{"name", sampler->name}, {"path", path}});
            }
        }
    }
    return {{"textures", std::move(textures)},
            {"typePath", material.type_path()},
            {"shaderPath", material.shader_path()},
            {"cullMode", schema::EnumNameCullMode(material.cull_mode())},
            {"blendMode", schema::EnumNameBlendMode(material.blend_mode())},
            {"parameters", std::move(parameters)}};
}

bool restore_material(Material material, const Json& saved)
{
    if (!material.set_type(saved["typePath"].get<std::string>()) ||
        !material.set_shader(saved["shaderPath"].get<std::string>()))
    {
        return false;
    }
    const auto cull = saved.value("cullMode", std::string("back"));
    if ((cull != "back" && cull != "front" && cull != "none") ||
        !material.set_cull_mode(cull == "back"    ? schema::CullMode::back
                                : cull == "front" ? schema::CullMode::front
                                                  : schema::CullMode::none))
    {
        return false;
    }
    const auto blend = saved.value("blendMode", std::string("opaque"));
    if ((blend != "opaque" && blend != "alpha_blend" && blend != "premultiplied") ||
        !material.set_blend_mode(blend == "opaque"        ? schema::BlendMode::opaque
                                 : blend == "alpha_blend" ? schema::BlendMode::alpha_blend
                                                          : schema::BlendMode::premultiplied))
    {
        return false;
    }
    for (const auto& parameter : saved["parameters"])
    {
        const auto name = parameter["name"].get<std::string>();
        const auto type = parameter["type"].get<std::string>();
        const auto& value = parameter["value"];
        bool success = false;
        if (type == "float")
        {
            success = material.set_float(name, value.get<float>());
        }
        else if (type == "v2")
        {
            success = material.set_v2(name, {value[0].get<double>(), value[1].get<double>()});
        }
        else if (type == "v3")
        {
            success = material.set_v3(
                name, {value[0].get<double>(), value[1].get<double>(), value[2].get<double>()});
        }
        else if (type == "v4")
        {
            success = material.set_v4(name, {value[0].get<double>(), value[1].get<double>(),
                                             value[2].get<double>(), value[3].get<double>()});
        }
        if (!success)
        {
            return false;
        }
    }
    for (const auto& texture : saved["textures"])
    {
        if (!material.set_texture(texture["name"].get<std::string>(),
                                  texture["path"].get<std::string>()))
        {
            return false;
        }
    }
    return true;
}

class MaterialTypeEdit final : public EditorEdit
{
public:
    Target target;
    std::size_t slot = 0;
    std::string path;
    Json before;
    Json after;
    std::vector<schema::AssetEntryT> versions;

    explicit MaterialTypeEdit(Target value) : target(value)
    {
    }

    bool apply(std::string& error) override
    {
        return change(before, after, error);
    }
    bool revert(std::string& error) override
    {
        return change(after, before, error);
    }
    bool retain_version(std::string_view resource_path)
    {
        auto* resources = ResourceSystem::instance();
        Guid guid;
        ResourcePtr resource;
        if (!resources || !ResourceSystem::path_to_guid(resource_path, guid) ||
            !resources->loader().load(guid, resource))
        {
            return false;
        }
        if (std::any_of(versions.begin(), versions.end(),
                        [&](const auto& entry) { return entry.guid == guid; }))
        {
            return true;
        }
        versions.push_back(resource->info());
        for (const auto& dependency : resource->info().dependencies)
        {
            std::string dependency_path;
            if (!ResourceSystem::guid_to_path(dependency, dependency_path) ||
                !retain_version(dependency_path))
            {
                return false;
            }
        }
        return true;
    }

private:
    bool change(const Json& expected, Json& replacement, std::string& error)
    {
        auto node = target.node(error);
        auto renderer = node ? node.get_component<MeshRenderer>() : MeshRenderer{};
        auto material = renderer ? renderer.material(slot) : Material{};
        auto* resources = ResourceSystem::instance();
        if (!material || !resources || material_json(material) != expected)
        {
            error = "Material changed outside this edit; undo or redo is stale";
            return false;
        }
        std::vector<ResourcePtr> pins;
        for (const auto& version : versions)
        {
            const auto* available = resources->catalog().find(version.guid);
            ResourcePtr resource;
            if (!available || *available != version ||
                !resources->loader().load(version.guid, resource))
            {
                error = "Material dependency version changed or is unavailable";
                return false;
            }
            pins.push_back(std::move(resource));
        }
        if (replacement.is_null())
        {
            if (!material.set_type(path))
            {
                error = "Cannot assign material type";
                return false;
            }
            replacement = material_json(material);
            return true;
        }
        if (!restore_material(material, replacement))
        {
            const bool restored = restore_material(material, expected);
            error =
                restored ? "Cannot restore material values" : "Cannot roll back material values";
            return false;
        }
        return true;
    }
};

} // namespace

bool prepare_material(WorldSystem& world, NodeAliases& aliases, const Json& arguments, Json& result,
                      std::unique_ptr<EditorEdit>& output, std::string& error)
{
    Target target(world, &aliases);
    if (!fields(arguments, {"sceneId", "nodeId", "name", "type", "value", "slot"}) ||
        !arguments.contains("name") || !arguments.contains("type") ||
        !arguments.contains("value") || !arguments["name"].is_string() ||
        !arguments["type"].is_string() || !target.read(arguments, error))
    {
        if (error.empty())
        {
            error = "set_material_parameter requires sceneId, nodeId, name, type and value";
        }
        return false;
    }
    const auto component = target.node(error).get_component<MeshRenderer>();
    if (!component)
    {
        error = "Node has no MeshRenderer";
        return false;
    }
    auto edit = std::make_unique<MaterialEdit>(target);
    edit->name = arguments["name"].get<std::string>();
    edit->type = arguments["type"].get<std::string>();
    if (!read_slot(arguments, edit->slot) || !component.material(edit->slot))
    {
        error = "Invalid material slot";
        return false;
    }
    const auto& material = component.material(edit->slot);
    edit->material_path = material.type_path();
    edit->shader_path = material.shader_path();
    if (!read_parameter(material, edit->name, edit->type, edit->before))
    {
        error = "Unknown material parameter or mismatched type";
        return false;
    }
    const std::size_t count = edit->type == "float" ? 1
                              : edit->type == "v2"  ? 2
                              : edit->type == "v3"  ? 3
                                                    : 4;
    if (!(count == 1 ? scalar(arguments["value"], edit->after[0])
                     : vector(arguments["value"], count, edit->after)) ||
        !std::all_of(edit->after.begin(), edit->after.end(), [](double value)
                     { return std::abs(value) <= std::numeric_limits<float>::max(); }))
    {
        error = "Material value must match its finite numeric type";
        return false;
    }
    if (count == 1)
    {
        edit->after[0] = static_cast<float>(edit->after[0]);
    }
    result = {{"sceneId", std::to_string(target.scene_id)}};
    output = std::move(edit);
    return true;
}

bool prepare_texture(WorldSystem& world, NodeAliases& aliases, const Json& arguments, Json& result,
                     std::unique_ptr<EditorEdit>& output, std::string& error)
{
    Target target(world, &aliases);
    if (!fields(arguments, {"sceneId", "nodeId", "name", "path", "slot"}) ||
        !arguments.contains("name") || !arguments["name"].is_string() ||
        !arguments.contains("path") || !arguments["path"].is_string() ||
        !target.read(arguments, error))
    {
        if (error.empty())
        {
            error = "set_material_texture requires name and resource path";
        }
        return false;
    }
    const auto renderer = target.node(error).get_component<MeshRenderer>();
    auto edit = std::make_unique<TextureEdit>(target);
    if (!renderer || !read_slot(arguments, edit->slot) || !renderer.material(edit->slot))
    {
        error = "Invalid material slot";
        return false;
    }
    const auto& material = renderer.material(edit->slot);
    edit->name = arguments["name"].get<std::string>();
    std::string_view previous_path;
    Guid guid;
    ResourcePtr replacement;
    auto* resources = ResourceSystem::instance();
    if (!material.get_texture(edit->name, previous_path) || !resources ||
        !ResourceSystem::path_to_guid(arguments["path"].get<std::string>(), guid) ||
        !resources->loader().load(guid, replacement) || !replacement->texture())
    {
        error = "Unknown texture slot or texture resource";
        return false;
    }
    edit->before = material.texture(edit->name)->info();
    edit->after = replacement->info();
    edit->material_path = material.type_path();
    edit->shader_path = material.shader_path();
    result = {{"sceneId", std::to_string(target.scene_id)}};
    output = std::move(edit);
    return true;
}

bool prepare_material_type(WorldSystem& world, NodeAliases& aliases, const Json& arguments,
                           Json& result, std::unique_ptr<EditorEdit>& output, std::string& error)
{
    Target target(world, &aliases);
    if (!fields(arguments, {"sceneId", "nodeId", "slot", "path"}) || !arguments.contains("path") ||
        !arguments["path"].is_string() || !target.read(arguments, error))
    {
        error = "set_material_type requires current sceneId, nodeId and resource path";
        return false;
    }
    auto record = std::make_unique<MaterialTypeEdit>(target);
    const auto renderer = target.node(error).get_component<MeshRenderer>();
    if (!renderer || !read_slot(arguments, record->slot) || !renderer.material(record->slot))
    {
        error = "Invalid material slot";
        return false;
    }
    const auto& material = renderer.material(record->slot);
    record->path = arguments["path"].get<std::string>();
    Guid guid;
    ResourcePtr requested;
    auto* resources = ResourceSystem::instance();
    if (!resources || !ResourceSystem::path_to_guid(record->path, guid) ||
        !resources->loader().load(guid, requested) || !requested->material())
    {
        error = "Requested path does not identify a material";
        return false;
    }
    record->before = material_json(material);
    if (!record->retain_version(material.type_path()) ||
        !record->retain_version(material.shader_path()) || !record->retain_version(record->path))
    {
        error = "Cannot capture material resource versions";
        return false;
    }
    for (const auto& texture : record->before["textures"])
    {
        if (!record->retain_version(texture["path"].get<std::string>()))
        {
            error = "Cannot capture material texture version";
            return false;
        }
    }
    result = {{"sceneId", arguments["sceneId"]},
              {"nodeId", arguments["nodeId"]},
              {"path", record->path},
              {"slot", record->slot}};
    output = std::move(record);
    return true;
}

bool prepare_material_blend(WorldSystem& world, NodeAliases& aliases, const Json& arguments,
                            Json& result, std::unique_ptr<EditorEdit>& output, std::string& error)
{
    Target target(world, &aliases);
    if (!fields(arguments, {"sceneId", "nodeId", "slot", "mode"}) || !arguments.contains("mode") ||
        (arguments["mode"] != "opaque" && arguments["mode"] != "alpha_blend" &&
         arguments["mode"] != "premultiplied") ||
        !target.read(arguments, error))
    {
        error =
            "set_material_blend_mode requires a target and opaque, alpha_blend or premultiplied";
        return false;
    }
    auto record = std::make_unique<MaterialTypeEdit>(target);
    const auto renderer = target.node(error).get_component<MeshRenderer>();
    if (!renderer || !read_slot(arguments, record->slot) || !renderer.material(record->slot))
    {
        error = "Invalid material slot";
        return false;
    }
    const auto& material = renderer.material(record->slot);
    record->before = material_json(material);
    record->after = record->before;
    record->after["blendMode"] = arguments["mode"];
    if (!record->retain_version(material.type_path()) ||
        !record->retain_version(material.shader_path()))
    {
        error = "Cannot retain the material and shader resource versions";
        return false;
    }
    for (const auto& texture : record->before["textures"])
    {
        if (!record->retain_version(texture["path"].get<std::string>()))
        {
            error = "Cannot retain material texture versions";
            return false;
        }
    }
    result = {{"sceneId", arguments["sceneId"]},
              {"nodeId", arguments["nodeId"]},
              {"slot", record->slot},
              {"mode", arguments["mode"]}};
    output = std::move(record);
    return true;
}

Json material_snapshot(const Material& material)
{
    return material_json(material);
}

bool restore_material_snapshot(Material material, const Json& snapshot)
{
    return restore_material(material, snapshot);
}
} // namespace engine::editor::components
