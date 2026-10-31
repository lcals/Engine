#include "plugins/editor/destruction/destruction_state_edit.hpp"

#include "plugins/runtime/destruction/destruction_component.hpp"
#include "runtime/scene/collider.hpp"
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

DestructionStateEdit::DestructionStateEdit(WorldSystem& world, std::uint64_t scene,
                                           std::shared_ptr<std::uint64_t> node,
                                           std::shared_ptr<std::uint64_t> revision,
                                           schema::ComponentDataT settings, ByteBuffer before,
                                           std::shared_ptr<std::uint64_t> support, float max_gap,
                                           float min_coverage)
    : DestructionStateEdit(world, scene, std::move(node), std::move(revision), std::move(settings),
                           std::move(before), false, {}, 0, 0, 0)
{
    support_ = std::move(support);
    max_gap_ = max_gap;
    min_coverage_ = min_coverage;
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
    if (no_change_)
    {
        return true;
    }
    if (after_.empty())
    {
        bool applied = false;
        if (support_)
        {
            const auto* support_node = component->node().scene().find_node(*support_);
            const auto* collider = support_node ? support_node->get_component<Collider>() : nullptr;
            if (!collider)
            {
                error = "Foundation support collider no longer exists";
                return false;
            }
            const auto before_revision = component->revision();
            applied = component->refresh_support(*collider, max_gap_, min_coverage_);
            no_change_ = applied && component->revision() == before_revision;
            if (no_change_)
            {
                return true;
            }
        }
        else
        {
            applied = reset_ ? component->reset()
                             : component->damage_sphere(center_, radius_, damage_, impulse_);
        }
        if (!applied)
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
    if (component && no_change_)
    {
        return true;
    }
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
