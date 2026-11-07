#include "editor/asset_pipeline/animation_sampling.hpp"

#include "runtime/animation/sampling_data.hpp"
#include "runtime/resource/codecs/animation_resource_codec.hpp"
#include "runtime/resource/codecs/skeleton_resource_codec.hpp"

namespace engine::editor::asset_pipeline
{
namespace
{
template <class T> bool archive(const T& data, std::vector<std::uint8_t>& output)
{
    ozz::io::MemoryStream stream;
    ozz::io::OArchive writer(&stream, ozz::kLittleEndian);
    writer << data;
    if (stream.Size() == 0 || stream.Size() > 64 * 1024 * 1024 ||
        stream.Seek(0, ozz::io::Stream::kSet) != 0)
    {
        return false;
    }
    output.resize(stream.Size());
    return stream.Read(output.data(), output.size()) == output.size();
}
} // namespace

bool prepare_skeleton_sampling(schema::SkeletonDataT& data, std::string& error)
{
    schema::SkeletonDataT candidate(data);
    candidate.schema_version = 2;
    candidate.sampling_encoding = schema::SamplingEncoding::structured;
    candidate.sampling_archive.clear();
    candidate.sampling_joint_order.clear();
    ozz::unique_ptr<ozz::animation::Skeleton> compiled;
    std::vector<std::size_t> order;
    if (!SkeletonResourceCodec::validate(candidate) ||
        !animation_detail::build_skeleton(candidate, compiled, order) ||
        !archive(*compiled, candidate.sampling_archive))
    {
        error = "Cannot compile skeleton sampling archive";
        return false;
    }
    candidate.sampling_encoding = schema::SamplingEncoding::ozz_archive;
    candidate.sampling_joint_order.assign(order.begin(), order.end());
    if (!SkeletonResourceCodec::validate(candidate))
    {
        error = "Invalid generated skeleton sampling archive";
        return false;
    }
    data = std::move(candidate);
    error.clear();
    return true;
}

bool prepare_animation_sampling(schema::AnimationDataT& data, const schema::SkeletonDataT& skeleton,
                                std::string& error)
{
    schema::AnimationDataT candidate(data);
    candidate.schema_version = 2;
    candidate.sampling_encoding = schema::SamplingEncoding::structured;
    candidate.sampling_archive.clear();
    candidate.sampling_channels.clear();
    ozz::unique_ptr<ozz::animation::Skeleton> rig;
    ozz::unique_ptr<ozz::animation::Animation> compiled;
    std::vector<std::size_t> order;
    if (!SkeletonResourceCodec::validate(skeleton) ||
        !AnimationResourceCodec::validate(candidate) ||
        !animation_detail::build_skeleton(skeleton, rig, order) ||
        !animation_detail::build_animation(candidate, skeleton, order, compiled) ||
        !archive(*compiled, candidate.sampling_archive))
    {
        error = "Cannot compile animation sampling archive";
        return false;
    }
    candidate.sampling_channels.resize(skeleton.joints.size());
    for (const auto& track : candidate.tracks)
    {
        candidate.sampling_channels[track->joint] = (!track->translations.empty() ? 1 : 0) |
                                                    (!track->rotations.empty() ? 2 : 0) |
                                                    (!track->scales.empty() ? 4 : 0);
    }
    candidate.sampling_encoding = schema::SamplingEncoding::ozz_archive;
    if (!AnimationResourceCodec::validate(candidate))
    {
        error = "Invalid generated animation sampling archive";
        return false;
    }
    data = std::move(candidate);
    error.clear();
    return true;
}
} // namespace engine::editor::asset_pipeline
