#include "editor/commands/components/component_edit.hpp"
#include "editor/asset_pipeline/components/scene_component_codec.hpp"
#include "runtime/resource/resource_system.hpp"

#include <algorithm>
#include <utility>

namespace engine::editor::components
{
bool fields(const Json& arguments, std::initializer_list<std::string_view> allowed)
{
    if (!arguments.is_object())
    {
        return false;
    }
    for (const auto& item : arguments.items())
    {
        if (std::find(allowed.begin(), allowed.end(), item.key()) == allowed.end())
        {
            return false;
        }
    }
    return true;
}

bool identifier(const Json& arguments, const char* name, std::uint64_t& value)
{
    if (!arguments.contains(name) || !arguments[name].is_string())
    {
        return false;
    }
    const auto& text = arguments[name].get_ref<const std::string&>();
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && value != 0 &&
           text == std::to_string(value);
}

bool scalar(const Json& input, double& output)
{
    if (!input.is_number())
    {
        return false;
    }
    output = input.get<double>();
    return std::isfinite(output);
}

bool vector(const Json& input, std::size_t count, std::array<double, 4>& values)
{
    if (!input.is_array() || input.size() != count)
    {
        return false;
    }
    for (std::size_t index = 0; index < count; ++index)
    {
        if (!scalar(input[index], values[index]))
        {
            return false;
        }
    }
    return true;
}

bool same(const Vec3& first, const Vec3& second)
{
    return first.x() == second.x() && first.y() == second.y() && first.z() == second.z();
}

bool same(const Color& first, const Color& second)
{
    return first.r() == second.r() && first.g() == second.g() && first.b() == second.b() &&
           first.a() == second.a();
}

Json json_vector(const Vec3& value)
{
    return Json::array({value.x(), value.y(), value.z()});
}

Node target(WorldSystem& world, const Json& arguments, std::string& error)
{
    auto* scene = world.scene();
    std::uint64_t node_id = 0;
    if (!scene || !arguments.is_object() || !arguments.contains("sceneId") ||
        arguments["sceneId"] != std::to_string(scene->id()) || !arguments.contains("nodeId") ||
        !arguments["nodeId"].is_string())
    {
        error = "Scene or node identity is missing or stale";
        return {};
    }
    const auto& text = arguments["nodeId"].get_ref<const std::string&>();
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), node_id);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
        text != std::to_string(node_id))
    {
        error = "Invalid node identity";
        return {};
    }
    auto node = scene->find_node(node_id);
    if (!node)
    {
        error = "Node not found";
    }
    return node;
}
bool fields(const Json& arguments, std::initializer_list<std::string_view> names,
            std::string& error)
{
    if (!arguments.is_object())
    {
        error = "Arguments must be an object";
        return false;
    }
    for (const auto& item : arguments.items())
    {
        if (item.key() != "sceneId" && item.key() != "nodeId" &&
            std::find(names.begin(), names.end(), item.key()) == names.end())
        {
            error = "Unknown component field: " + item.key();
            return false;
        }
    }
    return true;
}
bool number(const Json& input, float& value)
{
    if (!input.is_number())
    {
        return false;
    }
    const double parsed = input.get<double>();
    if (!std::isfinite(parsed) || std::abs(parsed) > std::numeric_limits<float>::max())
    {
        return false;
    }
    value = static_cast<float>(parsed);
    return true;
}
bool vector(const Json& input, Vec3& value)
{
    if (!input.is_array() || input.size() != 3)
    {
        return false;
    }
    double values[3]{};
    for (std::size_t i = 0; i < 3; ++i)
    {
        if (!input[i].is_number())
        {
            return false;
        }
        values[i] = input[i].get<double>();
        if (!std::isfinite(values[i]) || std::abs(values[i]) > std::numeric_limits<float>::max())
        {
            return false;
        }
    }
    value = Vec3(values[0], values[1], values[2]);
    return true;
}
ComponentEdit::ComponentEdit(WorldSystem& world, std::uint64_t scene,
                             std::shared_ptr<std::uint64_t> node,
                             std::optional<schema::ComponentDataT> before,
                             schema::ComponentDataT after,
                             std::vector<schema::AssetEntryT> versions,
                             ComponentType required_component,
                             std::vector<ResourceReference> prepared)
    : target_(world), type_(after.payload.type), before_(std::move(before)),
      after_(std::move(after)), versions_(std::move(versions)),
      required_component_(required_component), prepared_(std::move(prepared))
{
    target_.scene_id = scene;
    target_.identity = std::move(node);
}
bool ComponentEdit::apply(std::string& error)
{
    // 首次应用完成后不再由编辑历史保活载荷；重做按版本元数据重新获取。
    auto prepared = std::move(prepared_);
    return replace(before_, after_, error);
}
bool ComponentEdit::revert(std::string& error)
{
    return replace(after_, before_, error);
}
bool ComponentEdit::replace(const std::optional<schema::ComponentDataT>& expected,
                            const std::optional<schema::ComponentDataT>& value, std::string& error)
{
    auto node = target_.node(error);
    if (!node)
    {
        error = "Component edit targets a stale scene or node";
        return false;
    }
    std::vector<ResourceReference> pins;
    if (!resource_access::load_versions(target_.world.resource_loader(), versions_, pins))
    {
        error = "Component resource version changed since the edit";
        return false;
    }
    auto current = node.get_component(type_);
    schema::ComponentDataT snapshot;
    snapshot.flags = std::make_unique<schema::ComponentFlagsT>();
    if (bool(expected) != (current.valid()) ||
        (current && (!current.build_data(snapshot) || snapshot != *expected)))
    {
        error = "Component changed since the edit";
        return false;
    }
    if (!value)
    {
        return node.remove_component(type_);
    }
    if (required_component_ != ComponentType::NONE && !node.get_component(required_component_))
    {
        error = "The edited component requires another component on the same node";
        return false;
    }
    const bool created = !current;
    if (created)
    {
        current = target_.world.component_registry().create(type_, node);
    }
    if (!current || !current.restore_data(*value))
    {
        if (created && current)
        {
            (void)node.remove_component(type_);
        }
        error = "Cannot apply component settings or resolve its resources";
        return false;
    }
    return true;
}
bool resource_versions(WorldSystem& world, const schema::ComponentDataT& data,
                       std::vector<schema::AssetEntryT>& versions,
                       std::vector<ResourceReference>& prepared, std::string& error)
{
    const auto* codec = asset_pipeline::find_scene_component_codec(data.payload.type);
    if (!codec)
    {
        error = "Component codec is unavailable";
        return false;
    }
    toml::table fields;
    codec->write(data, fields);
    const auto capture = [&](std::string_view path)
    {
        if (path.empty())
        {
            return true;
        }
        for (const auto& resource : prepared)
        {
            if (resource && (resource->info().guid == path || resource->info().path == path))
            {
                return resource_access::collect_versions(resource, versions);
            }
        }
        ResourceReference resource;
        if (!resource_access::load(world.resource_loader(), path, resource) ||
            !resource_access::collect_versions(resource, versions))
        {
            return false;
        }
        prepared.push_back(std::move(resource));
        return true;
    };
    for (const auto name : codec->resource_fields())
    {
        const auto field = fields[name];
        if (const auto text = field.value<std::string>(); text && !capture(*text))
        {
            error = "Cannot prepare component resource: " + *text;
            return false;
        }
        if (const auto* items = field.as_array())
        {
            for (const auto& item : *items)
            {
                const auto text = item.value<std::string>();
                if (!text || !capture(*text))
                {
                    error = "Cannot prepare component resource array";
                    return false;
                }
            }
        }
    }
    return true;
}
} // namespace engine::editor::components
