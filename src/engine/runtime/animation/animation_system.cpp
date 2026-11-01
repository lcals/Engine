#include "runtime/animation/animation_system.hpp"

#include "runtime/animation/animation_clip.hpp"
#include "runtime/performance/profiling.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/animator.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/world/world_system.hpp"

#include <ozz/animation/runtime/blending_job.h>
#include <ozz/animation/runtime/local_to_model_job.h>
#include <ozz/animation/runtime/sampling_job.h>
#include <ozz/base/maths/soa_transform.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <map>
#include <set>
#include <vector>

namespace engine
{
namespace
{

bool active(const Node& node)
{
    for (const Node* current = &node; current != nullptr; current = current->parent())
    {
        const auto* transform = current->get_component<Transform>();
        if (transform == nullptr || !transform->enabled())
        {
            return false;
        }
    }
    return true;
}

bool advance(double delta_time, double duration, float speed, bool loop, double& seconds)
{
    if (speed <= 0.0F)
    {
        return true;
    }
    if (loop)
    {
        const double step = std::fmod(delta_time, duration / speed) * speed;
        seconds = std::fmod(seconds + step, duration);
        return true;
    }
    if (delta_time >= (duration - seconds) / speed)
    {
        seconds = duration;
        return false;
    }
    seconds += delta_time * speed;
    return true;
}

void store_matrix(const ozz::math::Float4x4& value, std::array<float, 16>& output)
{
    for (std::size_t column = 0; column < 4; ++column)
    {
        ozz::math::StorePtrU(value.cols[column], output.data() + column * 4);
    }
}

struct AnimationSampler
{
    std::shared_ptr<const animation_detail::CompiledAnimation> clip;
    std::unique_ptr<ozz::animation::SamplingJob::Context> context;
    std::vector<ozz::math::SoaTransform> locals;

    void prepare(std::shared_ptr<const animation_detail::CompiledAnimation> compiled)
    {
        clip = std::move(compiled);
        const auto& skeleton = *clip->target->skeleton;
        context = std::make_unique<ozz::animation::SamplingJob::Context>(skeleton.num_joints());
        locals.resize(skeleton.num_soa_joints());
    }

    bool sample(double seconds)
    {
        ozz::animation::SamplingJob job;
        job.animation = clip->animation.get();
        job.context = context.get();
        job.ratio = static_cast<float>(seconds / clip->animation->duration());
        job.output = ozz::make_span(locals);
        return job.Run();
    }
};

struct AnimationEntry
{
    AnimationSampler current;
    AnimationSampler previous;
    std::shared_ptr<const animation_detail::AnimationPose> frozen;
    std::vector<ozz::math::SoaTransform> locals;
    std::vector<ozz::math::Float4x4> models;
    std::vector<std::array<float, 16>> palette;
    std::vector<std::array<float, 16>> normal_palette;
    ozz::math::Float4x4 mesh_to_skeleton;
    ozz::math::Float4x4 skeleton_to_mesh;
    std::uint64_t mesh_revision = 0;
    std::uint64_t mesh_instance = 0;
    std::uint64_t deformation_revision = 0;
    bool blended = false;
    bool sampled = false;

    bool prepare(const schema::MeshDataT& mesh, std::uint64_t revision, std::uint64_t instance,
                 std::shared_ptr<const animation_detail::CompiledAnimation> clip)
    {
        if (!animation_detail::matrix(mesh.mesh_to_skeleton, mesh_to_skeleton) ||
            !animation_detail::inverse(mesh_to_skeleton, skeleton_to_mesh))
        {
            return false;
        }
        current.prepare(std::move(clip));
        const auto& skeleton = *current.clip->target->skeleton;
        locals.resize(skeleton.num_soa_joints());
        models.resize(skeleton.num_joints());
        palette.resize(models.size());
        normal_palette.resize(models.size());
        mesh_revision = revision;
        mesh_instance = instance;
        return true;
    }

    bool sample(double seconds, double previous_seconds, double blend)
    {
        if (!current.sample(seconds))
        {
            return false;
        }
        const auto& skeleton = *current.clip->target;
        blended = blend < 1.0;
        if (blended)
        {
            ozz::span<const ozz::math::SoaTransform> from;
            if (frozen != nullptr)
            {
                from = ozz::make_span(frozen->locals);
            }
            else if (previous.clip != nullptr)
            {
                if (!previous.sample(previous_seconds))
                {
                    return false;
                }
                from = ozz::make_span(previous.locals);
            }
            else
            {
                from = skeleton.skeleton->joint_rest_poses();
            }
            std::array<ozz::animation::BlendingJob::Layer, 2> layers;
            layers[0].transform = from;
            layers[0].weight = static_cast<float>(1.0 - blend);
            layers[1].transform = ozz::make_span(current.locals);
            layers[1].weight = static_cast<float>(blend);
            ozz::animation::BlendingJob job;
            job.layers = ozz::make_span(layers);
            job.rest_pose = skeleton.skeleton->joint_rest_poses();
            job.output = ozz::make_span(locals);
            if (!job.Run())
            {
                return false;
            }
        }
        else
        {
            locals = current.locals;
            previous = {};
            frozen.reset();
        }
        ozz::animation::LocalToModelJob local_to_model;
        local_to_model.skeleton = skeleton.skeleton.get();
        local_to_model.input = ozz::make_span(locals);
        local_to_model.output = ozz::make_span(models);
        if (!local_to_model.Run())
        {
            return false;
        }
        for (std::size_t dfs = 0; dfs < models.size(); ++dfs)
        {
            const auto index = skeleton.order[dfs];
            const auto transform =
                skeleton_to_mesh * models[dfs] * skeleton.inverse_bind[index] * mesh_to_skeleton;
            ozz::math::Float4x4 inverted;
            if (!animation_detail::inverse(transform, inverted))
            {
                return false;
            }
            store_matrix(transform, palette[index]);
            store_matrix(ozz::math::Transpose(inverted), normal_palette[index]);
            // 法线仅使用线性 inverse-transpose；去除齐次平移项，保留 affine 契约。
            auto& normal = normal_palette[index];
            normal[3] = normal[7] = normal[11] = 0.0F;
            normal[12] = normal[13] = normal[14] = 0.0F;
            normal[15] = 1.0F;
        }
        sampled = true;
        return true;
    }
};

} // namespace

struct AnimationSystem::State
{
    std::map<animation_detail::ResourceIdentity,
             std::shared_ptr<const animation_detail::CompiledSkeleton>>
        skeletons;
    std::map<animation_detail::AnimationIdentity,
             std::shared_ptr<const animation_detail::CompiledAnimation>>
        clips;
    std::map<std::uint64_t, std::unique_ptr<AnimationEntry>> entries;
    std::uint64_t scene_id = 0;

    bool compile(const ResourcePtr& clip, const schema::MeshDataT& mesh,
                 std::shared_ptr<const animation_detail::CompiledAnimation>& output)
    {
        output.reset();
        auto* resources = ResourceSystem::instance();
        ResourcePtr target;
        ResourcePtr source;
        if (resources == nullptr || clip == nullptr || clip->animation() == nullptr ||
            !resources->loader().load(mesh.skeleton_guid, target) || target == nullptr ||
            target->skeleton() == nullptr ||
            !resources->loader().load(clip->animation()->skeleton_guid, source) ||
            source == nullptr || source->skeleton() == nullptr ||
            !animation_detail::valid_binding(mesh, *target->skeleton(), *source->skeleton(),
                                             *clip->animation()))
        {
            return false;
        }
        const auto target_key = animation_detail::identity(*target);
        const animation_detail::AnimationIdentity key{
            animation_detail::identity(*clip), animation_detail::identity(*source), target_key};
        if (const auto found = clips.find(key); found != clips.end())
        {
            output = found->second;
            return true;
        }
        std::shared_ptr<const animation_detail::CompiledSkeleton> skeleton;
        const auto found = skeletons.find(target_key);
        if (found != skeletons.end())
        {
            skeleton = found->second;
        }
        else
        {
            auto candidate = std::make_shared<animation_detail::CompiledSkeleton>();
            if (!candidate->prepare(target))
            {
                return false;
            }
            skeleton = std::move(candidate);
        }
        auto candidate = std::make_shared<animation_detail::CompiledAnimation>();
        if (!candidate->prepare(clip, source, skeleton))
        {
            return false;
        }
        // 整个候选成功后发布；缓存只拥有不可变编译物，不含实例采样状态。
        skeletons.emplace(target_key, std::move(skeleton));
        output = std::move(candidate);
        clips.emplace(key, output);
        return true;
    }
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
    auto* scene = world_ != nullptr ? world_->scene() : nullptr;
    if (state_ != nullptr && scene != nullptr && state_->scene_id == scene->id())
    {
        for (const auto& [id, entry] : state_->entries)
        {
            (void)entry;
            auto* node = scene->find_node(id);
            auto* renderer = node != nullptr ? node->get_component<MeshRenderer>() : nullptr;
            auto* animator = node != nullptr ? node->get_component<Animator>() : nullptr;
            if (renderer != nullptr)
            {
                renderer->clear_deformation();
            }
            if (animator != nullptr)
            {
                animator->prepared_revision_ = 0;
                animator->target_skeleton_.reset();
            }
        }
    }
    if (state_ != nullptr)
    {
        state_->entries.clear();
        state_->clips.clear();
        state_->skeletons.clear();
        state_->scene_id = 0;
    }
}

bool AnimationSystem::on_tick(double delta_time)
{
    ENGINE_PROFILE_ZONE("Animation.tick");
    if (state_ == nullptr)
    {
        return false;
    }
    auto* scene = world_ != nullptr ? world_->scene() : nullptr;
    if (scene == nullptr)
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
        if (animator == nullptr || renderer == nullptr || !animator->enabled() ||
            !renderer->enabled() || !active(*node) || animator->animation_ == nullptr)
        {
            if (renderer != nullptr && state_->entries.contains(node->id()))
            {
                renderer->clear_deformation();
            }
            if (animator != nullptr)
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
        if (mesh == nullptr || mesh->skeleton_guid.empty() || mesh->skin.empty() || clip == nullptr)
        {
            std::fputs("Animation requires a skinned MeshRenderer and an animation resource\n",
                       stderr);
            clear();
            return false;
        }
        if (found == state_->entries.end() || animator->prepared_revision_ != animator->revision_ ||
            found->second->mesh_revision != renderer->mesh_revision() ||
            found->second->mesh_instance != renderer->mesh_instance_id())
        {
            std::shared_ptr<const animation_detail::CompiledAnimation> compiled;
            auto candidate = std::make_unique<AnimationEntry>();
            if (!state_->compile(animator->animation_, *mesh, compiled) ||
                !candidate->prepare(*mesh, renderer->mesh_revision(), renderer->mesh_instance_id(),
                                    compiled))
            {
                std::fputs("Cannot prepare animation: resource or skeleton mismatch\n", stderr);
                return false;
            }
            if (animator->fade_seconds_ > 0.0 && animator->previous_animation_ != nullptr)
            {
                const auto* previous =
                    found != state_->entries.end() ? found->second.get() : nullptr;
                const bool same_mesh = previous != nullptr && previous->sampled &&
                                       previous->mesh_instance == renderer->mesh_instance_id() &&
                                       previous->mesh_revision == renderer->mesh_revision();
                if (animator->frozen_pose_ != nullptr &&
                    animation_detail::identity(*animator->frozen_pose_->target) ==
                        animation_detail::identity(*compiled->target->resource))
                {
                    candidate->frozen = animator->frozen_pose_;
                }
                else if (same_mesh &&
                         (previous->blended ||
                          animation_detail::identity(*previous->current.clip->resource) !=
                              animation_detail::identity(*animator->previous_animation_)))
                {
                    // 打断过渡只冻结已显示的混合局部姿态，不积累更多采样层。
                    auto pose = std::make_shared<animation_detail::AnimationPose>();
                    pose->target = previous->current.clip->target->resource;
                    pose->locals = previous->locals;
                    candidate->frozen = std::move(pose);
                }
                else
                {
                    std::shared_ptr<const animation_detail::CompiledAnimation> source;
                    if (!state_->compile(animator->previous_animation_, *mesh, source))
                    {
                        return false;
                    }
                    candidate->previous.prepare(std::move(source));
                }
            }
            animator->target_skeleton_ = compiled->target->resource;
            animator->frozen_pose_ = candidate->frozen;
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
                return false;
            }
        }
        const bool running = animator->playback_ == AnimationPlayback::playing;
        if (running)
        {
            const bool playing = advance(delta_time, clip->duration, animator->settings().speed,
                                         animator->settings().loop, animator->time_);
            if (animator->fade_seconds_ > 0.0)
            {
                animator->fade_elapsed_ +=
                    std::min(delta_time, animator->fade_seconds_ - animator->fade_elapsed_);
                if (animator->previous_advancing_ && animator->previous_animation_ != nullptr)
                {
                    (void)advance(delta_time, animator->previous_animation_->animation()->duration,
                                  animator->previous_speed_, animator->previous_loop_,
                                  animator->previous_time_);
                }
            }
            // 短动作的末帧继续参与尚未完成的过渡，避免提前暂停在半混合姿态。
            if (!playing && animator->fade_elapsed_ >= animator->fade_seconds_)
            {
                animator->playback_ = AnimationPlayback::paused;
            }
        }
        if (running || animator->pose_dirty_ ||
            (animator->playback_ == AnimationPlayback::paused &&
             entry.deformation_revision != renderer->deformation_revision()))
        {
            const double blend = animator->fade_seconds_ > 0.0
                                     ? animator->fade_elapsed_ / animator->fade_seconds_
                                     : 1.0;
            if (!entry.sample(animator->time_, animator->previous_time_, blend) ||
                !renderer->set_skinning_palette(entry.palette, entry.normal_palette))
            {
                std::fputs("Animation sampling or palette submission failed\n", stderr);
                return false;
            }
            animator->pose_dirty_ = false;
            entry.deformation_revision = renderer->deformation_revision();
            if (blend >= 1.0)
            {
                animator->previous_animation_.reset();
                animator->frozen_pose_.reset();
                animator->fade_seconds_ = 0.0;
                animator->fade_elapsed_ = 0.0;
            }
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
