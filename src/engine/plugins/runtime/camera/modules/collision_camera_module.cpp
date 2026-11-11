#include "plugins/runtime/camera/modules/collision_camera_module.hpp"
#include "plugins/runtime/camera/camera_math.hpp"
#include "runtime/physics/physics_system.hpp"
namespace engine::plugins::camera
{
namespace
{
PhysicsQueryFilter filter(const schema::CameraCollisionDataT& d, const CameraEvaluationContext& c)
{
    PhysicsQueryFilter f;
    CameraTargetPose t;
    if (c.target(d.subject_slot, t))
    {
        f.ignore_node_id = t.node.id();
    }
    f.include_static = d.include_static;
    f.include_kinematic = d.include_kinematic;
    f.include_dynamic = d.include_dynamic;
    f.include_characters = d.include_characters;
    return f;
}
bool sweep(const schema::CameraCollisionDataT& d, const CameraEvaluationContext& c, Vec3 origin,
           Vec3 end, float& fraction)
{
    fraction = 1;
    auto displacement = detail::sub(end, origin);
    if (detail::length(displacement) < 1e-7)
    {
        return true;
    }
    PhysicsQueryHit hit;
    if (!c.physics->query_sphere(origin, displacement, d.radius + d.margin, filter(d, c), hit))
    {
        return false;
    }
    if (hit.hit)
    {
        fraction = std::max(0.0F, hit.fraction - 0.0001F);
    }
    return true;
}
} // namespace
bool CollisionCameraModule::validate(const Data& d, const schema::CameraControllerDataT& def,
                                     std::string& e)
{
    return (detail::slot(def, d.subject_slot, !d.sight_enabled) &&
            detail::finite(d.subject_offset) &&
            (!d.height_offset || detail::valid_input(d.height_offset.get(), def)) &&
            detail::bounded(d.radius, 0.001, 1000) && detail::bounded(d.margin, 0, 100) &&
            detail::bounded(d.minimum_distance, 0, 1e6) &&
            detail::bounded(d.contraction_damping, 0, 1000) &&
            detail::bounded(d.recovery_damping, 0, 1000) &&
            detail::bounded(d.recovery_delay, 0, 1000)) ||
           detail::fail(e, "collision: subject, radius, margin, distance or damping");
}
bool CollisionCameraModule::evaluate(const Data& d, const Runtime& current, Runtime& next,
                                     const CameraEvaluationContext& c, CameraPose& p)
{
    if (!d.motion_enabled && !d.sight_enabled)
    {
        return true;
    }
    if (!c.physics || !c.physics->is_initialized())
    {
        return false;
    }
    CameraTargetPose t;
    float height = 0;
    if (d.height_offset && !detail::input(d.height_offset.get(), c, height))
    {
        return false;
    }
    if (d.sight_enabled && !c.target(d.subject_slot, t))
    {
        return false;
    }
    if (d.sight_enabled)
    {
        t.position = detail::add(detail::add(t.position, d.subject_offset), Vec3(0, height, 0));
        const auto displacement = detail::sub(p.position, t.position);
        const double full = detail::length(displacement);
        float fraction;
        if (!sweep(d, c, t.position, p.position, fraction))
        {
            return false;
        }
        const double safe = full * fraction;
        if (safe < d.minimum_distance)
        {
            return false;
        }
        double distance = safe;
        if (!c.cut && current.distance > 0)
        {
            if (safe < current.distance)
            {
                next.recovery = 0;
                distance =
                    std::min(safe, std::lerp(double(current.distance), safe,
                                             detail::damping(c.delta_time, d.contraction_damping)));
            }
            else
            {
                next.recovery = current.recovery + c.delta_time;
                distance = next.recovery < d.recovery_delay
                               ? std::min(double(current.distance), safe)
                               : std::lerp(double(current.distance), safe,
                                           detail::damping(c.delta_time, d.recovery_damping));
            }
        }
        next.distance = float(distance);
        if (full > 1e-7)
        {
            p.position = detail::add(t.position, detail::mul(displacement, distance / full));
        }
    }
    if (d.motion_enabled)
    {
        float fraction;
        if (!sweep(d, c, c.previous.position, p.position, fraction))
        {
            return false;
        }
        p.position =
            detail::add(c.previous.position,
                        detail::mul(detail::sub(p.position, c.previous.position), fraction));
    }
    next.pose = p;
    return true;
}
bool CollisionCameraModule::check(const Data& d, const CameraEvaluationContext& c,
                                  const CameraPose& p, bool& accepted)
{
    accepted = false;
    if (!d.motion_enabled && !d.sight_enabled)
    {
        accepted = true;
        return true;
    }
    if (!c.physics || !c.physics->is_initialized())
    {
        return false;
    }
    std::vector<PhysicsObject> overlaps;
    if (!c.physics->overlap_sphere(p.position, d.radius, filter(d, c), overlaps))
    {
        return false;
    }
    if (!overlaps.empty())
    {
        return true;
    }
    float fraction;
    if (d.motion_enabled)
    {
        if (!sweep(d, c, c.previous.position, p.position, fraction))
        {
            return false;
        }
        if (fraction < 0.9998F)
        {
            return true;
        }
    }
    CameraTargetPose t;
    float height = 0;
    if (d.height_offset && !detail::input(d.height_offset.get(), c, height))
    {
        return false;
    }
    if (d.sight_enabled && !c.target(d.subject_slot, t))
    {
        return false;
    }
    if (d.sight_enabled)
    {
        t.position = detail::add(detail::add(t.position, d.subject_offset), Vec3(0, height, 0));
        if (detail::length(detail::sub(p.position, t.position)) < d.minimum_distance)
        {
            return true;
        }
        if (!sweep(d, c, t.position, p.position, fraction))
        {
            return false;
        }
        if (fraction < 0.9998F)
        {
            return true;
        }
    }
    accepted = true;
    return true;
}
} // namespace engine::plugins::camera
