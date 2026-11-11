#include "plugins/runtime/camera/modules/bounds_camera_module.hpp"
#include "plugins/runtime/camera/camera_math.hpp"
namespace engine::plugins::camera
{
namespace
{
bool frame(const schema::CameraBoundsDataT& d, const CameraEvaluationContext& c, Vec3& origin,
           detail::Q& q)
{
    origin = {};
    q = {0, 0, 0, 1};
    if (d.anchor_slot.empty())
    {
        return true;
    }
    CameraTargetPose t;
    if (!c.target(d.anchor_slot, t))
    {
        return false;
    }
    origin = t.position;
    q = t.rotation_xyzw;
    return true;
}
} // namespace
bool BoundsCameraModule::validate(const Data& d, const schema::CameraControllerDataT& def,
                                  std::string& e)
{
    return (detail::slot(def, d.anchor_slot, true) && detail::finite(d.minimum) &&
            detail::finite(d.maximum) && detail::bounded(d.margin, 0, 1e6) &&
            d.maximum.x() - d.minimum.x() >= 2 * d.margin &&
            d.maximum.y() - d.minimum.y() >= 2 * d.margin &&
            d.maximum.z() - d.minimum.z() >= 2 * d.margin) ||
           detail::fail(e, "bounds: anchor, extents or margin");
}
bool BoundsCameraModule::evaluate(const Data& d, const Runtime&, Runtime& next,
                                  const CameraEvaluationContext& c, CameraPose& p)
{
    Vec3 origin;
    detail::Q q;
    if (!frame(d, c, origin, q))
    {
        return false;
    }
    const detail::Q inv{-q[0], -q[1], -q[2], q[3]};
    auto local = detail::rotate(inv, detail::sub(p.position, origin));
    local = {std::clamp(local.x(), d.minimum.x() + d.margin, d.maximum.x() - d.margin),
             std::clamp(local.y(), d.minimum.y() + d.margin, d.maximum.y() - d.margin),
             std::clamp(local.z(), d.minimum.z() + d.margin, d.maximum.z() - d.margin)};
    p.position = detail::add(origin, detail::rotate(q, local));
    next.pose = p;
    return true;
}
bool BoundsCameraModule::check(const Data& d, const CameraEvaluationContext& c, const CameraPose& p,
                               bool& accepted)
{
    CameraPose test = p;
    Runtime current, next;
    accepted = false;
    if (!evaluate(d, current, next, c, test))
    {
        return false;
    }
    accepted = detail::length(detail::sub(test.position, p.position)) < 1e-4;
    return true;
}
} // namespace engine::plugins::camera
