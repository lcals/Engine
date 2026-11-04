#include "runtime/animation/animation_ik.hpp"
#include "runtime/animation/animation_ik_target.hpp"
namespace engine::animation_detail
{
using namespace ik_detail;
bool set_ik_target(Animator actor, std::string_view name, const AnimationIkTarget& target,
                   bool two_bone)
{
    auto renderer = actor.node().get_component<MeshRenderer>();
    const auto* mesh = renderer ? renderer.mesh_data() : nullptr;
    auto* resources = ResourceSystem::instance();
    ResourcePtr resource;
    std::vector<std::string> paths;
    std::vector<Definition> resolved;
    if (!mesh || !resources || !resources->loader().load(mesh->skeleton_guid, resource) ||
        !resource || !resource->skeleton() || !readable_joint_paths(*resource->skeleton(), paths) ||
        !definitions(*resource->skeleton(), paths, resolved))
    {
        return false;
    }
    auto found = std::find_if(resolved.begin(), resolved.end(), [&](const auto& item)
                              { return item.name == name && item.two_bone == two_bone; });
    if (found == resolved.end())
    {
        return false;
    }
    if (!target.enabled)
    {
        actor.runtime()->ik_targets_[std::string(name)] = {};
        return true;
    }
    if (!target_values(target))
    {
        return false;
    }
    IkOperation checked;
    if (!resolve_target(actor, *resource->skeleton(), paths, resolved,
                        static_cast<std::size_t>(found - resolved.begin()), target,
                        ozz::math::Float4x4::identity(), checked))
    {
        return false;
    }
    actor.runtime()->ik_targets_[std::string(name)] = target;
    return true;
}
} // namespace engine::animation_detail
