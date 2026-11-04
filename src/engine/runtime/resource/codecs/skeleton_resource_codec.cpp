#include "runtime/resource/codecs/skeleton_resource_codec.hpp"

#include "runtime/resource/codecs/model_value_utils.hpp"

#include <cmath>
#include <flatbuffers/verifier.h>
#include <map>
#include <set>
#include <string>
#include <utility>

namespace engine
{
namespace
{
bool named(std::string_view name)
{
    return !name.empty() && name.size() <= 1024 && name.find('\0') == std::string_view::npos;
}

bool unit_direction(const schema::Vec3& value)
{
    const double length = value.x() * value.x() + value.y() * value.y() + value.z() * value.z();
    return model_value_utils::vector(value) && std::abs(length - 1) < 0.001;
}

bool ancestor(const schema::SkeletonDataT& skeleton, std::size_t parent, std::size_t child)
{
    for (int index = skeleton.joints[child]->parent; index >= 0;
         index = skeleton.joints[index]->parent)
    {
        if (static_cast<std::size_t>(index) == parent)
        {
            return true;
        }
    }
    return false;
}
} // namespace

bool SkeletonResourceCodec::validate(const schema::SkeletonDataT& data)
{
    if (data.schema_version != 1 || !valid_path(data.path) || data.joints.empty() ||
        data.joints.size() > 1024 || data.sockets.size() > 1024 || data.aim_ik.size() > 1024 ||
        data.two_bone_ik.size() > 1024)
    {
        return false;
    }
    std::set<std::pair<int, std::string>> names;
    std::vector<std::string> paths;
    std::map<std::string, std::size_t, std::less<>> indices;
    for (std::size_t index = 0; index < data.joints.size(); ++index)
    {
        const auto& joint = data.joints[index];
        if (!joint || joint->name.empty() || joint->name.find('/') != std::string::npos ||
            joint->name.find('\0') != std::string::npos || joint->parent < -1 ||
            joint->parent >= static_cast<int>(index) ||
            !names.emplace(joint->parent, joint->name).second ||
            !model_value_utils::vector(joint->translation) ||
            !model_value_utils::quaternion(joint->rotation) ||
            !model_value_utils::scale(joint->scale) ||
            !model_value_utils::affine(joint->inverse_bind))
        {
            return false;
        }
        paths.push_back(joint->parent < 0 ? joint->name : paths[joint->parent] + "/" + joint->name);
        indices.emplace(paths.back(), index);
    }
    std::set<std::string_view> socket_names;
    for (const auto& socket : data.sockets)
    {
        if (!socket || !named(socket->name) || !socket_names.insert(socket->name).second ||
            !indices.contains(socket->joint_path) ||
            !model_value_utils::vector(socket->offset.translation()) ||
            !model_value_utils::quaternion(socket->offset.rotation()) ||
            !model_value_utils::scale(socket->offset.scale()))
        {
            return false;
        }
    }
    std::set<std::string_view> constraint_names;
    for (const auto& aim : data.aim_ik)
    {
        if (!aim || !named(aim->name) || !constraint_names.insert(aim->name).second ||
            !indices.contains(aim->joint_path) || !unit_direction(aim->forward_joint) ||
            !unit_direction(aim->up_joint) || !model_value_utils::vector(aim->offset_joint))
        {
            return false;
        }
        const double dot = aim->forward_joint.x() * aim->up_joint.x() +
                           aim->forward_joint.y() * aim->up_joint.y() +
                           aim->forward_joint.z() * aim->up_joint.z();
        if (std::abs(dot) >= 0.9999)
        {
            return false;
        }
    }
    for (const auto& chain : data.two_bone_ik)
    {
        if (!chain || !named(chain->name) || !constraint_names.insert(chain->name).second ||
            !unit_direction(chain->mid_axis_joint) || !std::isfinite(chain->soften) ||
            chain->soften < 0 || chain->soften > 1 || !std::isfinite(chain->twist_radians))
        {
            return false;
        }
        const auto start = indices.find(chain->start_path);
        const auto middle = indices.find(chain->middle_path);
        const auto end = indices.find(chain->end_path);
        if (start == indices.end() || middle == indices.end() || end == indices.end() ||
            !ancestor(data, start->second, middle->second) ||
            !ancestor(data, middle->second, end->second))
        {
            return false;
        }
    }
    return true;
}

bool SkeletonResourceCodec::decode(const ResourceCatalog&, ByteView payload, Resource& output) const
{
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
    flatbuffers::Verifier verifier(bytes, payload.size());
    if (!schema::VerifySkeletonDataBuffer(verifier) || !output.info().dependencies.empty())
    {
        return false;
    }
    auto data = schema::UnPackSkeletonData(bytes);
    if (data->path != output.info().path || !validate(*data))
    {
        return false;
    }
    store(output, std::move(data));
    return true;
}
} // namespace engine
