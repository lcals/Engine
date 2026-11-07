#include "runtime/animation/animation_ik.hpp"

#include "runtime/animation/animation_ik_target.hpp"
#include "runtime/resource/types/skeleton_resource.hpp"
#include "runtime/scene/animator_runtime.hpp"
#include "runtime/scene/resource_binding.hpp"
namespace engine::animation_detail
{
using namespace ik_detail;
bool set_ik_target(Animator actor, std::string_view name, const AnimationIkTarget& target,
                   bool two_bone)
{
    auto renderer = actor.node().get_component<MeshRenderer>();
    const auto* mesh = renderer ? renderer.mesh_data() : nullptr;
    auto* resources = actor.node().world()->resource_loader();
    resource_detail::ResourceReference resource;
    std::vector<std::string> paths;
    std::vector<Definition> resolved;
    if (!mesh || !resources ||
        !resource.dependency(resources, renderer.mesh_resource(), mesh->skeleton_guid) ||
        !resource || !resource.data<SkeletonResource>() ||
        !readable_joint_paths(*resource.data<SkeletonResource>(), paths) ||
        !definitions(*resource.data<SkeletonResource>(), paths, resolved))
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
        engine::ecs_detail::component_runtime(actor)->ik_targets_[std::string(name)] = {};
        return true;
    }
    if (!target_values(target))
    {
        return false;
    }
    IkOperation checked;
    if (!resolve_target(actor, *resource.data<SkeletonResource>(), paths, resolved,
                        static_cast<std::size_t>(found - resolved.begin()), target,
                        ozz::math::Float4x4::identity(), checked))
    {
        return false;
    }
    engine::ecs_detail::component_runtime(actor)->ik_targets_[std::string(name)] = target;
    return true;
}
} // namespace engine::animation_detail
