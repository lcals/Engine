#if defined(ENGINE_HAS_TERRAIN_PLUGIN) && ENGINE_HAS_TERRAIN_PLUGIN

#include "plugins/runtime/script/bindings/terrain_binding.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "plugins/runtime/terrain/terrain_component.hpp"
#include "plugins/runtime/terrain/terrain_system.hpp"
#include "runtime/app/app.hpp"
#include "runtime/scene/node.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

namespace engine::plugins::script::bindings
{
namespace
{

constexpr std::string_view javascript = R"js(
export class TerrainComponent extends Component {
    constructor(token, scene, node) { super(token, scene, node, native.TerrainKind); }
    DigSphere(x, y, z, radius) {
        const h = handles.get(this);
        return native.TerrainDig(h.scene, h.node, x, y, z, radius);
    }
    FillSphere(x, y, z, radius) {
        const h = handles.get(this);
        return native.TerrainFill(h.scene, h.node, x, y, z, radius);
    }
    DigSphereAsync(x, y, z, radius, completed) {
        const h = handles.get(this);
        return submitCompletion(() => native.TerrainDigAsync(h.scene, h.node, x, y, z, radius), completed);
    }
    FillSphereAsync(x, y, z, radius, completed) {
        const h = handles.get(this);
        return submitCompletion(() => native.TerrainFillAsync(h.scene, h.node, x, y, z, radius), completed);
    }
    CollapseDetached(minSamples, maxFragments, fragmentCells = 4, density = 1600) {
        const h = handles.get(this);
        return native.TerrainCollapse(h.scene, h.node, minSamples, maxFragments,
            fragmentCells, density);
    }
    get fragmentCount() { const h = handles.get(this); return native.TerrainFragments(h.scene, h.node); }
    Reset() { const h = handles.get(this); return native.TerrainReset(h.scene, h.node); }
    Save(slot) { const h = handles.get(this); return native.TerrainSave(h.scene, h.node, slot); }
    Load(slot) { const h = handles.get(this); return native.TerrainLoad(h.scene, h.node, slot); }
    get ready() { const h = handles.get(this); return native.TerrainReady(h.scene, h.node); }
    get revision() { const h = handles.get(this); return native.TerrainRevision(h.scene, h.node); }
    get chunkCount() { const h = handles.get(this); return native.TerrainChunks(h.scene, h.node); }
    get rebuiltChunks() {
        const h = handles.get(this); return native.TerrainRebuilt(h.scene, h.node);
    }
    get triangleCount() {
        const h = handles.get(this); return native.TerrainTriangles(h.scene, h.node);
    }
    get lastError() { const h = handles.get(this); return native.TerrainError(h.scene, h.node); }
}
componentTypes.set(TerrainComponent, { add: native.TerrainAdd, kind: native.TerrainKind });
Object.freeze(TerrainComponent.prototype);
Object.freeze(TerrainComponent);
)js";

terrain::TerrainComponent* component(const Arguments& arguments, int count)
{
    arguments.set_return(false);
    auto* node = BindingContext::node(arguments);
    return node != nullptr && arguments.count() == count
               ? node->get_component<terrain::TerrainComponent>()
               : nullptr;
}

bool status(const std::filesystem::path& path, std::filesystem::file_status& output)
{
    std::error_code error;
    output = std::filesystem::symlink_status(path, error);
    return !error || (error == std::errc::no_such_file_or_directory &&
                      output.type() == std::filesystem::file_type::not_found);
}

bool directory(const std::filesystem::path& path)
{
    std::filesystem::file_status state;
    if (!status(path, state) || !std::filesystem::is_directory(state))
    {
        return false;
    }
    std::error_code error;
    const auto canonical = std::filesystem::canonical(path, error);
    return !error && canonical == path;
}

bool slot_path(std::string_view slot, bool saving, std::filesystem::path& output)
{
    output.clear();
    if (!App::is_initialized() || slot.empty() || slot.size() > 48 ||
        !std::all_of(slot.begin(), slot.end(),
                     [](unsigned char character)
                     {
                         return (character >= 'A' && character <= 'Z') ||
                                (character >= 'a' && character <= 'z') ||
                                (character >= '0' && character <= '9') || character == '_' ||
                                character == '-';
                     }))
    {
        return false;
    }
    const auto root = App::persistent_data_path().lexically_normal();
    if (!root.is_absolute() || !directory(root))
    {
        return false;
    }
    const auto parent = root / "terrain";
    std::filesystem::file_status parent_status;
    if (!status(parent, parent_status))
    {
        return false;
    }
    std::error_code error;
    if (parent_status.type() == std::filesystem::file_type::not_found)
    {
        if (!saving)
        {
            return false;
        }
        std::filesystem::create_directory(parent, error);
        if (error)
        {
            return false;
        }
    }
    if (!directory(parent))
    {
        return false;
    }
    const auto candidate = parent / (std::string(slot) + ".bin");
    std::filesystem::file_status file_status;
    if (!status(candidate, file_status) ||
        (file_status.type() != std::filesystem::file_type::not_found &&
         !std::filesystem::is_regular_file(file_status)) ||
        (!saving && file_status.type() == std::filesystem::file_type::not_found))
    {
        return false;
    }
    const auto canonical = std::filesystem::weakly_canonical(candidate, error);
    if (error || canonical != candidate || canonical.parent_path() != parent)
    {
        return false;
    }
    output = candidate;
    return true;
}

void add(const Arguments& arguments)
{
    auto* node = BindingContext::node(arguments);
    arguments.set_return(arguments.count() == 2 && node != nullptr &&
                         node->add_component<terrain::TerrainComponent>() != nullptr);
}

void sphere(const Arguments& arguments)
{
    auto* value = component(arguments, 6);
    double x = 0;
    double y = 0;
    double z = 0;
    float radius = 0;
    if (value != nullptr && BindingContext::number(arguments, 2, x) &&
        BindingContext::number(arguments, 3, y) && BindingContext::number(arguments, 4, z) &&
        BindingContext::scalar(arguments, 5, radius) && radius > 0)
    {
        arguments.set_return(value->apply_sphere({x, y, z}, radius, arguments.data() != 0));
    }
}

void sphere_async(const Arguments& arguments)
{
    auto* value = component(arguments, 6);
    auto* context = BindingContext::current(arguments);
    auto* system = terrain::TerrainSystem::instance();
    double x = 0;
    double y = 0;
    double z = 0;
    float radius = 0;
    if (value && context && system && BindingContext::number(arguments, 2, x) &&
        BindingContext::number(arguments, 3, y) && BindingContext::number(arguments, 4, z) &&
        BindingContext::scalar(arguments, 5, radius))
    {
        arguments.set_return(context->submit_async(
            [=, fill = arguments.data() != 0](std::function<void(bool)> completed)
            {
                return system->request_apply_sphere(*value, {x, y, z}, radius, fill,
                                                    std::move(completed));
            }));
    }
}

void reset(const Arguments& arguments)
{
    auto* value = component(arguments, 2);
    arguments.set_return(value != nullptr && value->reset());
}

void persistence(const Arguments& arguments)
{
    auto* value = component(arguments, 3);
    std::string slot;
    std::filesystem::path path;
    const bool saving = arguments.data() != 0;
    if (value == nullptr || !value->ready() || !BindingContext::text(arguments, 2, slot) ||
        !slot_path(slot, saving, path))
    {
        return;
    }
    arguments.set_return(saving ? value->save_state(path) : value->load_state(path));
}

void ready(const Arguments& arguments)
{
    const auto* value = component(arguments, 2);
    arguments.set_return(value != nullptr && value->ready());
}

void collapse(const Arguments& arguments)
{
    auto* value = component(arguments, 6);
    float density = 0;
    if (value && arguments.is_uint32(2) && arguments.is_uint32(3) &&
        arguments.is_uint32(4) && BindingContext::scalar(arguments, 5, density))
    {
        arguments.set_return(value->collapse_detached(arguments.unsigned_integer(2),
            arguments.unsigned_integer(3), arguments.unsigned_integer(4), density));
    }
}

void statistic(const Arguments& arguments)
{
    const auto* value = component(arguments, 2);
    arguments.set_return({});
    if (value == nullptr)
    {
        return;
    }
    switch (arguments.data())
    {
    case 0:
        arguments.set_return(value->revision());
        break;
    case 1:
        arguments.set_return(static_cast<double>(value->chunk_count()));
        break;
    case 2:
        arguments.set_return(static_cast<double>(value->rebuilt_chunks()));
        break;
    case 3:
        arguments.set_return(static_cast<double>(value->triangle_count()));
        break;
    case 4:
        arguments.set_return(value->last_error());
        break;
    case 5:
        arguments.set_return(static_cast<double>(value->fragment_count()));
        break;
    }
}

} // namespace

std::string_view TerrainBinding::name() const noexcept
{
    return "TerrainComponent";
}

std::string_view TerrainBinding::source() const noexcept
{
    return javascript;
}

bool TerrainBinding::register_functions(vm::ScriptVm& vm) const
{
    return vm.integer("TerrainKind", static_cast<int>(terrain::TerrainComponent::component_type)) &&
           vm.function("TerrainAdd", add) && vm.function("TerrainDig", sphere, 0) &&
           vm.function("TerrainFill", sphere, 1) && vm.function("TerrainReset", reset) &&
           vm.function("TerrainDigAsync", sphere_async, 0) &&
           vm.function("TerrainFillAsync", sphere_async, 1) &&
           vm.function("TerrainCollapse", collapse) &&
           vm.function("TerrainFragments", statistic, 5) &&
           vm.function("TerrainSave", persistence, 1) &&
           vm.function("TerrainLoad", persistence, 0) && vm.function("TerrainReady", ready) &&
           vm.function("TerrainRevision", statistic, 0) &&
           vm.function("TerrainChunks", statistic, 1) &&
           vm.function("TerrainRebuilt", statistic, 2) &&
           vm.function("TerrainTriangles", statistic, 3) &&
           vm.function("TerrainError", statistic, 4);
}

} // namespace engine::plugins::script::bindings

#endif
