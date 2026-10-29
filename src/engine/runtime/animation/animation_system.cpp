#include "runtime/animation/animation_system.hpp"

#include <ozz/animation/runtime/skeleton.h>

namespace engine
{

AnimationSystem::AnimationSystem() noexcept = default;

AnimationSystem::~AnimationSystem() noexcept
{
    shutdown();
}

bool AnimationSystem::on_initialize(const EngineConfig&)
{
    // 空骨架验证运行时对象的构造与释放；尚不加载动画资源。
    const ozz::animation::Skeleton skeleton;
    return skeleton.num_joints() == 0;
}

void AnimationSystem::on_shutdown() noexcept
{
}

} // namespace engine
