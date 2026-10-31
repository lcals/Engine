#include "plugins/editor/destruction/destruction_state_edit.hpp"

#include "plugins/runtime/destruction/destruction_component.hpp"
#include "runtime/scene/node.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/world/world_system.hpp"

#include <utility>

namespace engine::plugins::destruction
{

DestructionStateEdit::DestructionStateEdit(WorldSystem& world, std::uint64_t scene,
                                           std::shared_ptr<std::uint64_t> node,
                                           std::shared_ptr<std::uint64_t> revision,
                                           schema::ComponentDataT settings, ByteBuffer before,
                                           bool reset, Vec3 center, float radius, float damage,
                                           float impulse)
    : world_(world), scene_(scene), node_(std::move(node)), revision_(std::move(revision)),
      settings_(std::move(settings)), before_(std::move(before)), reset_(reset), center_(center),
      radius_(radius), damage_(damage), impulse_(impulse)
{
}

bool DestructionStateEdit::affects_scene() const noexcept
{
    return false;
}

DestructionComponent* DestructionStateEdit::target(std::string& error)
{
    auto* scene = world_.scene();
    auto* node = scene && scene->id() == scene_ ? scene->find_node(*node_) : nullptr;
    auto* component = node ? node->get_component<DestructionComponent>() : nullptr;
    schema::ComponentDataT settings;
    if (!component || !component->ready() || component->revision() != *revision_ ||
        !component->build_data(settings) || settings != settings_)
    {
        error = "Destruction scene, node, settings or damage changed outside the edit history";
        return nullptr;
    }
    return component;
}

bool DestructionStateEdit::apply(std::string& error)
{
    auto* component = target(error);
    if (!component)
    {
        return false;
    }
    if (after_.empty())
    {
        if (!(reset_ ? component->reset()
                     : component->damage_sphere(center_, radius_, damage_, impulse_)))
        {
            error = component->last_error();
            return false;
        }
        if (!component->snapshot(after_))
        {
            error = "Cannot capture the resulting destruction state";
            if (!component->restore(before_))
            {
                error += "; failed to roll back the operation";
            }
            *revision_ = component->revision();
            return false;
        }
    }
    else if (!component->restore(after_))
    {
        error = component->last_error();
        return false;
    }
    *revision_ = component->revision();
    return true;
}

bool DestructionStateEdit::revert(std::string& error)
{
    auto* component = target(error);
    if (!component || !component->restore(before_))
    {
        if (component)
        {
            error = component->last_error();
        }
        return false;
    }
    *revision_ = component->revision();
    return true;
}

} // namespace engine::plugins::destruction
