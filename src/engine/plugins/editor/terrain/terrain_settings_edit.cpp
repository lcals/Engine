#include "plugins/editor/terrain/terrain_settings_edit.hpp"

#include "plugins/runtime/terrain/terrain_component.hpp"
#include "runtime/scene/node.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/world/world_system.hpp"

#include <utility>

namespace engine::plugins::terrain
{

TerrainSettingsEdit::TerrainSettingsEdit(WorldSystem& world, std::uint64_t scene,
                                         std::shared_ptr<std::uint64_t> node,
                                         std::optional<schema::ComponentDataT> before,
                                         schema::ComponentDataT after, ByteBuffer before_state,
                                         ByteBuffer after_state)
    : world_(world), scene_(scene), node_(std::move(node)), before_(std::move(before)),
      after_(std::move(after)), before_state_(std::move(before_state)),
      after_state_(std::move(after_state))
{
}

bool TerrainSettingsEdit::apply(std::string& error)
{
    return replace(before_, after_, before_state_, after_state_, error);
}

bool TerrainSettingsEdit::revert(std::string& error)
{
    return replace(after_, before_, after_state_, before_state_, error);
}

bool TerrainSettingsEdit::replace(const std::optional<schema::ComponentDataT>& expected,
                                  const std::optional<schema::ComponentDataT>& replacement,
                                  const ByteBuffer& expected_state,
                                  const ByteBuffer& replacement_state, std::string& error)
{
    auto* scene = world_.scene();
    auto* node = scene != nullptr && scene->id() == scene_ ? scene->find_node(*node_) : nullptr;
    if (node == nullptr)
    {
        error = "Terrain settings target a stale scene or node";
        return false;
    }
    auto* component = node->get_component<TerrainComponent>();
    schema::ComponentDataT current;
    ByteBuffer current_state;
    if (static_cast<bool>(expected) != (component != nullptr) ||
        (component != nullptr && (!component->build_data(current) || current != *expected)) ||
        (!expected_state.empty() &&
         (!component->snapshot(current_state) || current_state != expected_state)))
    {
        error = "Terrain settings or volume changed since the edit";
        return false;
    }
    if (!replacement)
    {
        if (!node->remove_component(TerrainComponent::component_type))
        {
            error = "Cannot remove terrain component";
            return false;
        }
        return true;
    }
    const bool created = component == nullptr;
    if (created)
    {
        component = node->add_component<TerrainComponent>();
    }
    if (component == nullptr)
    {
        error = "Cannot create terrain component";
        return false;
    }
    if (!component->restore_data(*replacement))
    {
        error = component->last_error();
        if (created && !node->remove_component(TerrainComponent::component_type))
        {
            error += "; cannot remove the incomplete terrain component";
        }
        return false;
    }
    if (!replacement_state.empty() && !component->restore(replacement_state))
    {
        error = component->last_error();
        if (created)
        {
            if (!node->remove_component(TerrainComponent::component_type))
            {
                error += "; cannot remove the incomplete terrain component";
            }
        }
        else if (!component->restore_data(*expected) ||
                 (!expected_state.empty() && !component->restore(expected_state)))
        {
            error += "; failed to restore the previous terrain state";
        }
        return false;
    }
    return true;
}

} // namespace engine::plugins::terrain
