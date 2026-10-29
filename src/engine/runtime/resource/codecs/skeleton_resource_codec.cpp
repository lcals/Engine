#include "runtime/resource/codecs/skeleton_resource_codec.hpp"

#include "runtime/resource/codecs/model_value_utils.hpp"

#include <flatbuffers/verifier.h>
#include <set>
#include <string>
#include <utility>

namespace engine
{
bool SkeletonResourceCodec::validate(const schema::SkeletonDataT& data)
{
    if (data.schema_version != 1 || !valid_path(data.path) || data.joints.empty() ||
        data.joints.size() > 1024)
    {
        return false;
    }
    std::set<std::pair<int, std::string>> names;
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
