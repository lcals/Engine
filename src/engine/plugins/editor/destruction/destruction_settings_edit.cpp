#include "plugins/editor/destruction/destruction_settings_edit.hpp"

#include "plugins/runtime/destruction/destruction_component.hpp"
#include "plugins/runtime/destruction/destruction_system.hpp"
#include "runtime/scene/node.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/world/world_system.hpp"

#include <utility>

namespace engine::plugins::destruction
{

DestructionSettingsEdit::DestructionSettingsEdit(WorldSystem& world, std::uint64_t scene,
                                                 std::shared_ptr<std::uint64_t> node,
                                                 std::shared_ptr<std::uint64_t> revision,
                                                 std::optional<schema::ComponentDataT> before,
                                                 schema::ComponentDataT after,
                                                 ByteBuffer before_state)
    : world_(world), scene_(scene), node_(std::move(node)), revision_(std::move(revision)),
      before_(std::move(before)), after_(std::move(after)), before_state_(std::move(before_state))
{
}

bool DestructionSettingsEdit::apply(std::string& error)
{
    return replace(before_, after_, after_state_, !captured_, error);
}

bool DestructionSettingsEdit::revert(std::string& error)
{
    return replace(after_, before_, before_state_, false, error);
}

bool DestructionSettingsEdit::replace(const std::optional<schema::ComponentDataT>& expected,
                                      const std::optional<schema::ComponentDataT>& replacement,
                                      const ByteBuffer& replacement_state, bool capture,
                                      std::string& error)
{
    auto* scene = world_.scene();
    auto node = scene && scene->id() == scene_ ? scene->find_node(*node_) : Node{};
    if (!node)
    {
        error = "Destruction settings target a stale scene or node";
        return false;
    }
    auto component = node.get_component<DestructionComponent>();
    schema::ComponentDataT current;
    current.flags = std::make_unique<schema::ComponentFlagsT>();
    ByteBuffer rollback;
    if (static_cast<bool>(expected) != (component.valid()) ||
        (component &&
         (component.revision() != *revision_ || !component.build_data(current) ||
          current != *expected || (component.ready() && !component.snapshot(rollback)))))
    {
        error = "Destruction settings or damage changed outside the edit history";
        return false;
    }
    auto* system = DestructionSystem::instance();
    if (!system)
    {
        error = "Destruction system is unavailable";
        return false;
    }
    if (!replacement)
    {
        if (!node.remove_component(DestructionComponent::component_type))
        {
            error = "Cannot remove destruction component";
            return false;
        }
        if (!system->synchronize(node))
        {
            error = "Cannot release generated destruction groups";
            component = node.add_component<DestructionComponent>();
            if (!component || !component.restore_data(*expected) ||
                (!rollback.empty() && !component.restore(rollback)) || !system->synchronize(node))
            {
                error += "; failed to restore the removed component and its previous state";
            }
            *revision_ = component ? component.revision() : 0;
            return false;
        }
        *revision_ = 0;
        return true;
    }
    const bool created = !component;
    if (created)
    {
        component = node.add_component<DestructionComponent>();
    }
    if (!component)
    {
        error = "Cannot create destruction component";
        return false;
    }
    bool success = component.restore_data(*replacement) && system->synchronize(node);
    if (success && !component.settings().destruction_guid.empty() && !component.ready())
    {
        // An explicit edit also captures initially disabled objects. Generated visuals and
        // colliders keep the component's disabled state; the host's lazy policy is unchanged.
        success = component.reset();
    }
    if (success && !replacement_state.empty())
    {
        success = component.restore(replacement_state);
    }
    if (success && capture && component.ready())
    {
        success = component.snapshot(after_state_);
    }
    if (!success)
    {
        error = component.last_error().empty() ? "Cannot publish destruction settings"
                                               : component.last_error();
        if (created)
        {
            if (!node.remove_component(DestructionComponent::component_type))
            {
                error += "; cannot remove the incomplete component";
                *revision_ = component.revision();
            }
            else
            {
                if (!system->synchronize(node))
                {
                    error += "; cannot release incomplete generated groups";
                }
                *revision_ = 0;
            }
        }
        else
        {
            if (!component.restore_data(*expected) ||
                (!rollback.empty() && !component.restore(rollback)) || !system->synchronize(node))
            {
                error += "; failed to restore the previous destruction state";
            }
            *revision_ = component.revision();
        }
        return false;
    }
    captured_ = captured_ || capture;
    *revision_ = component.revision();
    return true;
}

} // namespace engine::plugins::destruction
