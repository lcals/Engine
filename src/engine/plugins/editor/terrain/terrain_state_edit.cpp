#include "plugins/editor/terrain/terrain_state_edit.hpp"

#include "plugins/runtime/terrain/terrain_component.hpp"
#include "runtime/scene/node.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/world/world_system.hpp"

#include <utility>

namespace engine::plugins::terrain
{

TerrainStateEdit::TerrainStateEdit(WorldSystem& world, std::uint64_t scene,
                                   std::shared_ptr<std::uint64_t> node,
                                   schema::ComponentDataT settings, ByteBuffer before,
                                   ByteBuffer after)
    : world_(world), scene_(scene), node_(std::move(node)), settings_(std::move(settings)),
      before_(std::move(before)), after_(std::move(after))
{
}

bool TerrainStateEdit::affects_scene() const noexcept
{
    return false;
}

bool TerrainStateEdit::apply(std::string& error)
{
    return replace(before_, after_, error);
}

bool TerrainStateEdit::revert(std::string& error)
{
    return replace(after_, before_, error);
}

bool TerrainStateEdit::replace(const ByteBuffer& expected, const ByteBuffer& value,
                               std::string& error)
{
    auto* scene = world_.scene();
    auto node = scene != nullptr && scene->id() == scene_ ? scene->find_node(*node_) : Node{};
    auto component = node.valid() ? node.get_component<TerrainComponent>() : TerrainComponent{};
    if (!component || !component.ready())
    {
        error = "Terrain edit targets a stale scene, node or unavailable terrain";
        return false;
    }
    schema::ComponentDataT settings;
    settings.flags = std::make_unique<schema::ComponentFlagsT>();
    ByteBuffer current;
    if (!component.build_data(settings) || settings != settings_ || !component.snapshot(current) ||
        current != expected)
    {
        error = "Terrain changed since the edit; refusing to overwrite its current state";
        return false;
    }
    if (!component.restore(value))
    {
        error = component.last_error();
        if (error.empty())
        {
            error = "Cannot restore terrain edit";
        }
        return false;
    }
    return true;
}

} // namespace engine::plugins::terrain
