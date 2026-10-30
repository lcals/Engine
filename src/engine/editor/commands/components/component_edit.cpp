#include "editor/commands/components/component_edit.hpp"

#include <algorithm>
#include <utility>

namespace engine::editor::components
{
Node* target(WorldSystem& world, const Json& arguments, std::string& error)
{
    auto* scene = world.scene();
    std::uint64_t node_id = 0;
    if (!scene || !arguments.is_object() || !arguments.contains("sceneId") ||
        arguments["sceneId"] != std::to_string(scene->id()) || !arguments.contains("nodeId") ||
        !arguments["nodeId"].is_string())
    {
        error = "Scene or node identity is missing or stale";
        return nullptr;
    }
    const auto& text = arguments["nodeId"].get_ref<const std::string&>();
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), node_id);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
        text != std::to_string(node_id))
    {
        error = "Invalid node identity";
        return nullptr;
    }
    auto* node = scene->find_node(node_id);
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
                             schema::ComponentDataT after)
    : world_(world), scene_(scene), node_(std::move(node)), type_(after.payload.type),
      before_(std::move(before)), after_(std::move(after))
{
}
bool ComponentEdit::apply(std::string& error)
{
    return replace(before_, after_, error);
}
bool ComponentEdit::revert(std::string& error)
{
    return replace(after_, before_, error);
}
bool ComponentEdit::replace(const std::optional<schema::ComponentDataT>& expected,
                            const std::optional<schema::ComponentDataT>& value, std::string& error)
{
    auto* scene = world_.scene();
    auto* node = scene && scene->id() == scene_ ? scene->find_node(*node_) : nullptr;
    if (!node)
    {
        error = "Component edit targets a stale scene or node";
        return false;
    }
    auto* current = node->get_component(type_);
    schema::ComponentDataT snapshot;
    if (bool(expected) != (current != nullptr) ||
        (current && (!current->build_data(snapshot) || snapshot != *expected)))
    {
        error = "Component changed since the edit";
        return false;
    }
    if (!value)
    {
        return node->remove_component(type_);
    }
    const bool created = current == nullptr;
    if (created)
    {
        current = world_.component_registry().create(type_, *node);
    }
    if (!current || !current->restore_data(*value))
    {
        if (created && current)
        {
            (void)node->remove_component(type_);
        }
        error = "Cannot apply component settings or resolve its resources";
        return false;
    }
    return true;
}
} // namespace engine::editor::components
