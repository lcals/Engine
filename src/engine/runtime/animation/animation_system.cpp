#include "runtime/animation/animation_system.hpp"

#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/animator.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/utils/mesh_utils.hpp"
#include "runtime/world/world_system.hpp"

#include <ozz/animation/offline/animation_builder.h>
#include <ozz/animation/offline/raw_animation.h>
#include <ozz/animation/offline/raw_skeleton.h>
#include <ozz/animation/offline/skeleton_builder.h>
#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/local_to_model_job.h>
#include <ozz/animation/runtime/sampling_job.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/base/maths/simd_math.h>
#include <ozz/base/maths/soa_transform.h>
#include <ozz/geometry/runtime/skinning_job.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace engine
{
namespace
{
ozz::math::Float3 vector(const Vec3& value)
{
    return {static_cast<float>(value.x()), static_cast<float>(value.y()),
            static_cast<float>(value.z())};
}
ozz::math::Quaternion quaternion(const Vec4& value)
{
    return {static_cast<float>(value.x()), static_cast<float>(value.y()),
            static_cast<float>(value.z()), static_cast<float>(value.w())};
}
bool compressible(const Vec3& value, bool scale)
{
    const auto valid = [scale](double scalar)
    {
        return std::isfinite(scalar) && std::abs(scalar) <= 65504.0 &&
               (!scale || std::abs(scalar) >= 0x1p-24);
    };
    return valid(value.x()) && valid(value.y()) && valid(value.z());
}
bool matrix(const std::vector<float>& values, ozz::math::Float4x4& output)
{
    if (values.size() != 16 ||
        !std::all_of(values.begin(), values.end(),
                     [](float value) { return std::isfinite(value); }) ||
        std::abs(values[3]) > 1.0e-6F || std::abs(values[7]) > 1.0e-6F ||
        std::abs(values[11]) > 1.0e-6F || std::abs(values[15] - 1.0F) > 1.0e-6F)
    {
        return false;
    }
    for (std::size_t index = 0; index < 4; ++index)
    {
        output.cols[index] = ozz::math::simd_float4::LoadPtrU(values.data() + index * 4);
    }
    return true;
}
bool inverse(const ozz::math::Float4x4& value, ozz::math::Float4x4& output)
{
    ozz::math::SimdInt4 invertible;
    output = ozz::math::Invert(value, &invertible);
    return ozz::math::AreAllTrue1(invertible);
}
bool joint_paths(const schema::SkeletonDataT& skeleton, std::vector<std::string>& output)
{
    output.clear();
    if (skeleton.joints.empty() ||
        skeleton.joints.size() > static_cast<std::size_t>(ozz::animation::Skeleton::kMaxJoints))
    {
        return false;
    }
    std::set<std::string> names;
    for (std::size_t index = 0; index < skeleton.joints.size(); ++index)
    {
        const auto* joint = skeleton.joints[index].get();
        if (!joint || joint->parent < -1 || joint->parent >= static_cast<int>(index))
        {
            return false;
        }
        std::string path = joint->parent < 0 ? std::string{} : output[joint->parent];
        path += std::to_string(joint->name.size()) + ":" + joint->name;
        if (!names.insert(path).second)
        {
            return false;
        }
        output.push_back(std::move(path));
    }
    return true;
}
void build_joint(const schema::SkeletonDataT& data, std::size_t index,
                 const std::vector<std::vector<std::size_t>>& children,
                 ozz::animation::offline::RawSkeleton::Joint& output,
                 std::vector<std::size_t>& order)
{
    const auto& joint = *data.joints[index];
    order.push_back(index);
    output.name = joint.name.c_str();
    output.transform.translation = vector(joint.translation);
    output.transform.rotation = quaternion(joint.rotation);
    output.transform.scale = vector(joint.scale);
    output.children.resize(children[index].size());
    for (std::size_t child = 0; child < children[index].size(); ++child)
    {
        build_joint(data, children[index][child], children, output.children[child], order);
    }
}
bool active(const Node& node)
{
    for (const Node* current = &node; current; current = current->parent())
    {
        const auto* transform = current->get_component<Transform>();
        if (!transform || !transform->enabled())
        {
            return false;
        }
    }
    return true;
}
bool normalize(double& x, double& y, double& z)
{
    const double length = std::hypot(x, y, z);
    if (!std::isfinite(length) || length <= 1.0e-12)
    {
        return false;
    }
    x /= length;
    y /= length;
    z /= length;
    return true;
}
} // namespace

struct AnimationSystem::State
{
    struct Entry
    {
        ResourcePtr clip_resource;
        ResourcePtr skeleton_resource;
        ResourcePtr source_skeleton_resource;
        std::uint64_t mesh_revision = 0;
        std::uint64_t mesh_instance = 0;
        std::uint64_t deformation_revision = 0;
        ozz::unique_ptr<ozz::animation::Skeleton> skeleton;
        ozz::unique_ptr<ozz::animation::Animation> animation;
        std::unique_ptr<ozz::animation::SamplingJob::Context> context;
        std::vector<std::size_t> order;
        std::vector<ozz::math::SoaTransform> locals;
        std::vector<ozz::math::Float4x4> models;
        std::vector<ozz::math::Float4x4> bind_inverse;
        std::vector<ozz::math::Float4x4> palette;
        std::vector<ozz::math::Float4x4> normal_palette;
        ozz::math::Float4x4 mesh_to_skeleton;
        ozz::math::Float4x4 skeleton_to_mesh;
        std::vector<std::uint16_t> indices;
        std::vector<float> weights;
        std::vector<float> positions;
        std::vector<float> normals;
        std::vector<float> tangents;
        std::vector<float> bitangents;
        std::vector<float> output_positions;
        std::vector<float> output_normals;
        std::vector<float> output_tangents;
        std::vector<float> output_bitangents;
        std::vector<schema::Vec4> bind_tangents;
        std::vector<schema::MeshVertex> vertices;
        std::vector<schema::Vec4> deformed_tangents;

        bool prepare(ResourcePtr clip, ResourcePtr target, ResourcePtr source,
                     const schema::MeshDataT& mesh, std::uint64_t revision, std::uint64_t instance)
        {
            const auto* skeleton_data = target ? target->skeleton() : nullptr;
            const auto* source_data = source ? source->skeleton() : nullptr;
            const auto* clip_data = clip ? clip->animation() : nullptr;
            if (!skeleton_data || !source_data || !clip_data || mesh.skin.empty() ||
                mesh.skin.size() != mesh.vertices.size() ||
                mesh.vertices.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
                !matrix(mesh.mesh_to_skeleton, mesh_to_skeleton) ||
                !inverse(mesh_to_skeleton, skeleton_to_mesh) ||
                !MeshUtils::build_tangents(mesh, bind_tangents))
            {
                return false;
            }
            std::vector<std::string> target_paths;
            std::vector<std::string> source_paths;
            if (!joint_paths(*skeleton_data, target_paths) ||
                !joint_paths(*source_data, source_paths))
            {
                return false;
            }
            std::map<std::string, std::size_t> source_indices;
            for (std::size_t index = 0; index < source_paths.size(); ++index)
            {
                source_indices.emplace(source_paths[index], index);
            }
            const auto count = skeleton_data->joints.size();
            std::vector<std::vector<std::size_t>> children(count);
            std::vector<std::size_t> roots;
            for (std::size_t index = 0; index < count; ++index)
            {
                const auto parent = skeleton_data->joints[index]->parent;
                if (parent < 0)
                {
                    roots.push_back(index);
                }
                else
                {
                    children[parent].push_back(index);
                }
            }
            ozz::animation::offline::RawSkeleton raw_skeleton;
            raw_skeleton.roots.resize(roots.size());
            for (std::size_t index = 0; index < roots.size(); ++index)
            {
                build_joint(*skeleton_data, roots[index], children, raw_skeleton.roots[index],
                            order);
            }
            skeleton = ozz::animation::offline::SkeletonBuilder{}(raw_skeleton);
            if (!skeleton || static_cast<std::size_t>(skeleton->num_joints()) != count)
            {
                return false;
            }
            std::vector<const schema::AnimationTrackDataT*> tracks(source_paths.size(), nullptr);
            for (const auto& track : clip_data->tracks)
            {
                if (!track || track->joint >= tracks.size() || tracks[track->joint])
                {
                    return false;
                }
                tracks[track->joint] = track.get();
            }
            ozz::animation::offline::RawAnimation raw_animation;
            raw_animation.name = clip_data->name.c_str();
            raw_animation.duration = clip_data->duration;
            raw_animation.tracks.resize(count);
            for (std::size_t dfs = 0; dfs < count; ++dfs)
            {
                const auto target_index = order[dfs];
                const auto found = source_indices.find(target_paths[target_index]);
                if (found == source_indices.end())
                {
                    return false;
                }
                const auto* track = tracks[found->second];
                const auto& rest = *skeleton_data->joints[target_index];
                auto& result = raw_animation.tracks[dfs];
                if (track)
                {
                    for (const auto& key : track->translations)
                    {
                        result.translations.push_back({key.time(), vector(key.value())});
                    }
                    for (const auto& key : track->rotations)
                    {
                        result.rotations.push_back({key.time(), quaternion(key.value())});
                    }
                    for (const auto& key : track->scales)
                    {
                        result.scales.push_back({key.time(), vector(key.value())});
                    }
                }
                // ozz 空通道默认单位变换；缺失通道必须明确填目标关节绑定局部姿态。
                if (result.translations.empty())
                {
                    if (!compressible(rest.translation, false))
                    {
                        return false;
                    }
                    result.translations.push_back({0.0F, vector(rest.translation)});
                }
                if (result.rotations.empty())
                {
                    result.rotations.push_back({0.0F, quaternion(rest.rotation)});
                }
                if (result.scales.empty())
                {
                    if (!compressible(rest.scale, true))
                    {
                        return false;
                    }
                    result.scales.push_back({0.0F, vector(rest.scale)});
                }
            }
            animation = ozz::animation::offline::AnimationBuilder{}(raw_animation);
            if (!animation || animation->num_tracks() != skeleton->num_joints())
            {
                return false;
            }
            context =
                std::make_unique<ozz::animation::SamplingJob::Context>(skeleton->num_joints());
            locals.resize(skeleton->num_soa_joints());
            models.resize(count);
            palette.resize(count);
            normal_palette.resize(count);
            bind_inverse.resize(count);
            for (std::size_t index = 0; index < count; ++index)
            {
                if (!matrix(skeleton_data->joints[index]->inverse_bind, bind_inverse[index]))
                {
                    return false;
                }
            }
            for (std::size_t index = 0; index < mesh.vertices.size(); ++index)
            {
                const auto& vertex = mesh.vertices[index];
                positions.insert(positions.end(), {vertex.x(), vertex.y(), vertex.z()});
                normals.insert(normals.end(), {vertex.nx(), vertex.ny(), vertex.nz()});
                const auto& tangent = bind_tangents[index];
                tangents.insert(tangents.end(),
                                {static_cast<float>(tangent.x()), static_cast<float>(tangent.y()),
                                 static_cast<float>(tangent.z())});
                bitangents.insert(
                    bitangents.end(),
                    {static_cast<float>((vertex.ny() * tangent.z() - vertex.nz() * tangent.y()) *
                                        tangent.w()),
                     static_cast<float>((vertex.nz() * tangent.x() - vertex.nx() * tangent.z()) *
                                        tangent.w()),
                     static_cast<float>((vertex.nx() * tangent.y() - vertex.ny() * tangent.x()) *
                                        tangent.w())});
                const auto& skin = mesh.skin[index];
                if (skin.j0() >= count || skin.j1() >= count || skin.j2() >= count ||
                    skin.j3() >= count)
                {
                    return false;
                }
                indices.insert(indices.end(), {skin.j0(), skin.j1(), skin.j2(), skin.j3()});
                const float sum = skin.w0() + skin.w1() + skin.w2() + skin.w3();
                const float influence_weights[] = {skin.w0(), skin.w1(), skin.w2(), skin.w3()};
                if (!std::isfinite(sum) || std::abs(sum - 1.0F) > 0.0001F ||
                    !std::all_of(
                        std::begin(influence_weights), std::end(influence_weights), [](float value)
                        { return std::isfinite(value) && value >= 0.0F && value <= 1.0F; }))
                {
                    return false;
                }
                float w0 = skin.w0() / sum, w1 = skin.w1() / sum, w2 = skin.w2() / sum;
                const float first_three = w0 + w1 + w2;
                if (first_three > 1.0F)
                {
                    w0 /= first_three;
                    w1 /= first_three;
                    w2 = std::max(0.0F, 1.0F - w0 - w1);
                }
                // SkinningJob 从前三项恢复第四项权重。
                weights.insert(weights.end(), {w0, w1, w2});
            }
            output_positions.resize(positions.size());
            output_normals.resize(normals.size());
            output_tangents.resize(tangents.size());
            output_bitangents.resize(bitangents.size());
            vertices.resize(mesh.vertices.size());
            deformed_tangents.resize(mesh.vertices.size());
            clip_resource = std::move(clip);
            skeleton_resource = std::move(target);
            source_skeleton_resource = std::move(source);
            mesh_revision = revision;
            mesh_instance = instance;
            return true;
        }

        bool sample(double seconds)
        {
            ozz::animation::SamplingJob sampling;
            sampling.animation = animation.get();
            sampling.context = context.get();
            sampling.ratio = static_cast<float>(seconds / animation->duration());
            sampling.output = ozz::make_span(locals);
            if (!sampling.Run())
            {
                return false;
            }
            ozz::animation::LocalToModelJob local_to_model;
            local_to_model.skeleton = skeleton.get();
            local_to_model.input = ozz::make_span(locals);
            local_to_model.output = ozz::make_span(models);
            if (!local_to_model.Run())
            {
                return false;
            }
            for (std::size_t dfs = 0; dfs < models.size(); ++dfs)
            {
                const auto index = order[dfs];
                palette[index] =
                    skeleton_to_mesh * models[dfs] * bind_inverse[index] * mesh_to_skeleton;
                ozz::math::Float4x4 inverse_matrix;
                if (!inverse(palette[index], inverse_matrix))
                {
                    return false;
                }
                normal_palette[index] = ozz::math::Transpose(inverse_matrix);
            }
            ozz::geometry::SkinningJob skinning;
            skinning.vertex_count = static_cast<int>(vertices.size());
            skinning.influences_count = 4;
            skinning.joint_matrices = ozz::make_span(palette);
            skinning.joint_inverse_transpose_matrices = ozz::make_span(normal_palette);
            skinning.joint_indices = ozz::make_span(indices);
            skinning.joint_indices_stride = 4 * sizeof(std::uint16_t);
            skinning.joint_weights = ozz::make_span(weights);
            skinning.joint_weights_stride = 3 * sizeof(float);
            skinning.in_positions = ozz::make_span(positions);
            skinning.in_positions_stride = 3 * sizeof(float);
            skinning.in_normals = ozz::make_span(normals);
            skinning.in_normals_stride = 3 * sizeof(float);
            skinning.out_positions = ozz::make_span(output_positions);
            skinning.out_positions_stride = 3 * sizeof(float);
            skinning.out_normals = ozz::make_span(output_normals);
            skinning.out_normals_stride = 3 * sizeof(float);
            if (!skinning.Run())
            {
                return false;
            }
            // 切线使用普通线性矩阵，不能套用法线 inverse-transpose。
            skinning.joint_inverse_transpose_matrices = {};
            skinning.in_normals = ozz::make_span(tangents);
            skinning.out_normals = ozz::make_span(output_tangents);
            skinning.in_tangents = ozz::make_span(bitangents);
            skinning.in_tangents_stride = 3 * sizeof(float);
            skinning.out_tangents = ozz::make_span(output_bitangents);
            skinning.out_tangents_stride = 3 * sizeof(float);
            if (!skinning.Run())
            {
                return false;
            }
            for (std::size_t index = 0; index < vertices.size(); ++index)
            {
                const auto offset = index * 3;
                double nx = output_normals[offset], ny = output_normals[offset + 1],
                       nz = output_normals[offset + 2];
                if (!normalize(nx, ny, nz))
                {
                    return false;
                }
                double tx = output_tangents[offset], ty = output_tangents[offset + 1],
                       tz = output_tangents[offset + 2];
                const double dot = nx * tx + ny * ty + nz * tz;
                tx -= nx * dot;
                ty -= ny * dot;
                tz -= nz * dot;
                if (!normalize(tx, ty, tz))
                {
                    return false;
                }
                vertices[index] =
                    schema::MeshVertex(output_positions[offset], output_positions[offset + 1],
                                       output_positions[offset + 2], static_cast<float>(nx),
                                       static_cast<float>(ny), static_cast<float>(nz));
                const double handedness = (ny * tz - nz * ty) * output_bitangents[offset] +
                                          (nz * tx - nx * tz) * output_bitangents[offset + 1] +
                                          (nx * ty - ny * tx) * output_bitangents[offset + 2];
                if (!std::isfinite(handedness) || std::abs(handedness) <= 1.0e-12)
                {
                    return false;
                }
                deformed_tangents[index] = Vec4(tx, ty, tz, handedness < 0.0 ? -1.0 : 1.0);
            }
            return true;
        }
    };

    std::map<std::uint64_t, std::unique_ptr<Entry>> entries;
    std::uint64_t scene_id = 0;
};

AnimationSystem::AnimationSystem(WorldSystem* world) noexcept : world_(world)
{
}
AnimationSystem::~AnimationSystem() noexcept
{
    shutdown();
}
bool AnimationSystem::on_initialize(const EngineConfig&)
{
    state_ = std::make_unique<State>();
    return true;
}
void AnimationSystem::clear() noexcept
{
    auto* scene = world_ ? world_->scene() : nullptr;
    if (state_ && scene && state_->scene_id == scene->id())
    {
        for (const auto& [id, entry] : state_->entries)
        {
            (void)entry;
            auto* node = scene->find_node(id);
            auto* renderer = node ? node->get_component<MeshRenderer>() : nullptr;
            auto* animator = node ? node->get_component<Animator>() : nullptr;
            if (renderer)
            {
                renderer->clear_deformation();
            }
            if (animator)
            {
                animator->prepared_revision_ = 0;
                animator->target_skeleton_.reset();
            }
        }
    }
    if (state_)
    {
        state_->entries.clear();
        state_->scene_id = 0;
    }
}
bool AnimationSystem::on_tick(double delta_time)
{
    if (!state_)
    {
        return false;
    }
    auto* scene = world_ ? world_->scene() : nullptr;
    if (!scene)
    {
        clear();
        return true;
    }
    if (state_->scene_id != scene->id())
    {
        clear();
        state_->scene_id = scene->id();
    }
    std::set<std::uint64_t> retained;
    for (const auto& node : scene->nodes())
    {
        auto* animator = node->get_component<Animator>();
        auto* renderer = node->get_component<MeshRenderer>();
        if (!animator || !renderer || !animator->enabled() || !renderer->enabled() ||
            !active(*node) || !animator->animation_)
        {
            if (renderer && state_->entries.contains(node->id()))
            {
                renderer->clear_deformation();
            }
            if (animator)
            {
                animator->prepared_revision_ = 0;
                animator->target_skeleton_.reset();
            }
            continue;
        }
        retained.insert(node->id());
        const auto* mesh = renderer->mesh_data();
        const auto* clip = animator->animation_->animation();
        auto found = state_->entries.find(node->id());
        if (!mesh || mesh->skeleton_guid.empty() || mesh->skin.empty() || !clip)
        {
            std::fputs("Animation requires a skinned MeshRenderer and an animation resource\n",
                       stderr);
            clear();
            return false;
        }
        if (found == state_->entries.end() || animator->prepared_revision_ != animator->revision_ ||
            found->second->clip_resource != animator->animation_ ||
            found->second->mesh_revision != renderer->mesh_revision() ||
            found->second->mesh_instance != renderer->mesh_instance_id())
        {
            auto* resources = ResourceSystem::instance();
            ResourcePtr target;
            ResourcePtr source;
            auto candidate = std::make_unique<State::Entry>();
            if (!resources || !resources->loader().load(mesh->skeleton_guid, target) ||
                !resources->loader().load(clip->skeleton_guid, source) ||
                !candidate->prepare(animator->animation_, target, source, *mesh,
                                    renderer->mesh_revision(), renderer->mesh_instance_id()))
            {
                std::fputs("Cannot prepare animation: skeleton hierarchy, resources or skin data "
                           "mismatch\n",
                           stderr);
                clear();
                return false;
            }
            animator->target_skeleton_ = std::move(target);
            animator->prepared_revision_ = animator->revision_;
            state_->entries[node->id()] = std::move(candidate);
            found = state_->entries.find(node->id());
            animator->pose_dirty_ =
                animator->playback_ != AnimationPlayback::stopped || animator->pose_dirty_;
        }
        auto& entry = *found->second;
        if (animator->awake_pending_)
        {
            animator->awake_pending_ = false;
            if (animator->settings().play_on_awake && !animator->play())
            {
                clear();
                return false;
            }
        }
        const bool running = animator->playback_ == AnimationPlayback::playing;
        if (running)
        {
            const double duration = clip->duration;
            const double speed = animator->settings().speed;
            if (speed > 0.0 && animator->settings().loop)
            {
                const double step = std::fmod(delta_time, duration / speed) * speed;
                animator->time_ = std::fmod(animator->time_ + step, duration);
            }
            else if (speed > 0.0)
            {
                const double remaining = duration - animator->time_;
                if (delta_time >= remaining / speed)
                {
                    animator->time_ = duration;
                    animator->playback_ = AnimationPlayback::paused;
                }
                else
                {
                    animator->time_ += delta_time * speed;
                }
            }
        }
        if (running || animator->pose_dirty_ ||
            (animator->playback_ == AnimationPlayback::paused &&
             entry.deformation_revision != renderer->deformation_revision()))
        {
            if (!entry.sample(animator->time_) ||
                !renderer->set_deformation(entry.vertices, entry.deformed_tangents))
            {
                std::fputs("Animation sampling or skinning failed\n", stderr);
                clear();
                return false;
            }
            animator->pose_dirty_ = false;
            entry.deformation_revision = renderer->deformation_revision();
        }
    }
    for (auto it = state_->entries.begin(); it != state_->entries.end();)
    {
        it = retained.contains(it->first) ? std::next(it) : state_->entries.erase(it);
    }
    return true;
}
void AnimationSystem::on_shutdown() noexcept
{
    clear();
    state_.reset();
}
} // namespace engine
