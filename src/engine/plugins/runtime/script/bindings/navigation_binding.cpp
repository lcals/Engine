#include "plugins/runtime/script/bindings/navigation_binding.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "runtime/navigation/navigation_system.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/rigid_body.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/world/world_system.hpp"

#include <nlohmann/json.hpp>

#include <charconv>
#include <cmath>
#include <limits>
#include <set>

namespace engine::plugins::script::bindings
{
namespace
{
constexpr std::string_view javascript = R"js(
function navigationNodes(nodes) {
    if (!Array.isArray(nodes) || nodes.length > 512) return null;
    const result = [];
    for (const node of nodes) {
        const h = handles.get(node);
        if (!(node instanceof Node) || !h) return null;
        result.push({ scene: h.scene.toString(), node: h.node.toString() });
    }
    return JSON.stringify(result);
}
export class Navigation {
    constructor() { throw new TypeError('Navigation is static'); }
    static Build(nodes, options) {
        const values = navigationNodes(nodes);
        return native.NavigationBuild(values, JSON.stringify(options));
    }
    static Replace(nodes, minimum, maximum) {
        const values = navigationNodes(nodes);
        return native.NavigationReplace(values, JSON.stringify({minimum,maximum}));
    }
    static FindPath(start, end) {
        return native.NavigationPath(start?.x, start?.y, start?.z, end?.x, end?.y, end?.z);
    }
    static Clear() { return native.NavigationClear(); }
    static get ready() { return native.NavigationReady(); }
    static get revision() { return native.NavigationRevision(); }
    static get rebuiltTiles() { return native.NavigationRebuilt(); }
    static get lastError() { return native.NavigationError(); }
}
Object.freeze(Navigation.prototype);
Object.freeze(Navigation);
)js";

NavigationSystem* system(const Arguments& arguments)
{
    auto* context = BindingContext::current(arguments);
    auto* world = context ? context->world() : nullptr;
    const auto* scene = world ? world->scene() : nullptr;
    return scene && scene->id() == context->scene_id() ? context->navigation() : nullptr;
}

bool json_argument(const Arguments& arguments, int index, nlohmann::json& output)
{
    std::string text;
    if (!BindingContext::text(arguments, index, text) || text.size() > 128 * 1024)
    {
        return false;
    }
    output = nlohmann::json::parse(text, nullptr, false);
    return !output.is_discarded();
}

bool scalar(const nlohmann::json& object, const char* key, float& output, bool required = false)
{
    const auto value = object.find(key);
    if (value == object.end())
    {
        return !required;
    }
    if (!value->is_number())
    {
        return false;
    }
    const double number = value->get<double>();
    if (!std::isfinite(number) || std::abs(number) > std::numeric_limits<float>::max())
    {
        return false;
    }
    output = static_cast<float>(number);
    return true;
}

bool vector(const nlohmann::json& object, const char* key, Vec3& output)
{
    const auto found = object.find(key);
    float x = 0;
    float y = 0;
    float z = 0;
    if (found == object.end() || !found->is_object() || !scalar(*found, "x", x, true) ||
        !scalar(*found, "y", y, true) || !scalar(*found, "z", z, true))
    {
        return false;
    }
    output = {x, y, z};
    return true;
}

bool identifier(const nlohmann::json& object, const char* key, std::uint64_t& output)
{
    const auto value = object.find(key);
    if (value == object.end() || !value->is_string())
    {
        return false;
    }
    const auto& text = value->get_ref<const std::string&>();
    const auto result = std::from_chars(text.data(), text.data() + text.size(), output);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size() && output != 0;
}

bool geometry(const Arguments& arguments, std::vector<NavigationGeometry>& output)
{
    nlohmann::json values;
    auto* context = BindingContext::current(arguments);
    auto* scene = context && context->world() ? context->world()->scene() : nullptr;
    if (!scene || scene->id() != context->scene_id() || !json_argument(arguments, 0, values) ||
        !values.is_array() || values.size() > 512)
    {
        return false;
    }
    std::set<std::uint64_t> seen;
    for (const auto& value : values)
    {
        std::uint64_t scene_id = 0;
        std::uint64_t node_id = 0;
        if (!value.is_object() || !identifier(value, "scene", scene_id) ||
            !identifier(value, "node", node_id) || scene_id != scene->id() ||
            !seen.insert(node_id).second)
        {
            return false;
        }
        const auto* node = scene->find_node(node_id);
        const auto* renderer = node ? node->get_component<MeshRenderer>() : nullptr;
        const auto* transform = node ? node->get_component<Transform>() : nullptr;
        const auto* body = node ? node->get_component<RigidBody>() : nullptr;
        if (!renderer || !renderer->active() || !transform || !transform->active() ||
            !renderer->mesh_data() ||
            (body && body->settings().motion != schema::BodyMotion::static_body))
        {
            return false;
        }
        NavigationGeometry item;
        item.mesh = renderer->mesh_data();
        if (!transform->world_matrix(item.transform.data()))
        {
            return false;
        }
        output.push_back(item);
    }
    return true;
}

void build(const Arguments& arguments)
{
    arguments.set_return(false);
    auto* navigation = system(arguments);
    std::vector<NavigationGeometry> meshes;
    nlohmann::json options;
    NavigationBounds bounds;
    if (!navigation || arguments.count() != 2 || !geometry(arguments, meshes) ||
        !json_argument(arguments, 1, options) || !options.is_object() ||
        !vector(options, "minimum", bounds.minimum) ||
        !vector(options, "maximum", bounds.maximum))
    {
        // A malformed replacement cannot identify safe retained tiles.
        if (navigation)
        {
            navigation->clear();
        }
        return;
    }
    if (arguments.data() != 0)
    {
        arguments.set_return(navigation->replace(meshes, bounds));
        return;
    }
    NavigationSettings settings;
    float tile_size = static_cast<float>(settings.tile_size);
    if (!scalar(options, "cellSize", settings.cell_size) ||
        !scalar(options, "cellHeight", settings.cell_height) ||
        !scalar(options, "agentHeight", settings.agent_height) ||
        !scalar(options, "agentRadius", settings.agent_radius) ||
        !scalar(options, "agentMaxClimb", settings.agent_max_climb) ||
        !scalar(options, "agentMaxSlope", settings.agent_max_slope) ||
        !scalar(options, "tileSize", tile_size) || tile_size < 1 || tile_size > 256 ||
        std::floor(tile_size) != tile_size)
    {
        navigation->clear();
        return;
    }
    settings.tile_size = static_cast<std::uint32_t>(tile_size);
    arguments.set_return(navigation->build(meshes, bounds, settings));
}

void path(const Arguments& arguments)
{
    arguments.set_return({});
    const auto* navigation = system(arguments);
    double coordinates[6]{};
    if (!navigation || arguments.count() != 6)
    {
        return;
    }
    for (int index = 0; index < 6; ++index)
    {
        if (!BindingContext::number(arguments, index, coordinates[index]))
        {
            return;
        }
    }
    std::vector<Vec3> points;
    if (!navigation->find_path({coordinates[0], coordinates[1], coordinates[2]},
                               {coordinates[3], coordinates[4], coordinates[5]}, points))
    {
        return;
    }
    vm::NativeValue::Array output;
    for (const auto& point : points)
    {
        output.emplace_back(vm::NativeValue::Object{
            {"x", point.x()}, {"y", point.y()}, {"z", point.z()}});
    }
    arguments.set_return(std::move(output));
}

void clear(const Arguments& arguments)
{
    auto* navigation = system(arguments);
    const bool valid = navigation && arguments.count() == 0;
    if (valid)
    {
        navigation->clear();
    }
    arguments.set_return(valid);
}

void statistic(const Arguments& arguments)
{
    arguments.set_return(arguments.data() == 0 ? vm::NativeValue(false) : vm::NativeValue{});
    const auto* navigation = system(arguments);
    if (!navigation || arguments.count() != 0)
    {
        return;
    }
    switch (arguments.data())
    {
    case 0:
        arguments.set_return(navigation->ready());
        break;
    case 1:
        arguments.set_return(navigation->revision());
        break;
    case 2:
        arguments.set_return(static_cast<double>(navigation->rebuilt_tiles()));
        break;
    case 3:
        arguments.set_return(navigation->last_error());
        break;
    }
}
} // namespace

std::string_view NavigationBinding::name() const noexcept
{
    return "Navigation";
}
std::string_view NavigationBinding::source() const noexcept
{
    return javascript;
}
bool NavigationBinding::register_functions(vm::ScriptVm& vm) const
{
    return vm.function("NavigationBuild", build, 0) &&
           vm.function("NavigationReplace", build, 1) && vm.function("NavigationPath", path) &&
           vm.function("NavigationClear", clear) && vm.function("NavigationReady", statistic, 0) &&
           vm.function("NavigationRevision", statistic, 1) &&
           vm.function("NavigationRebuilt", statistic, 2) &&
           vm.function("NavigationError", statistic, 3);
}
} // namespace engine::plugins::script::bindings
