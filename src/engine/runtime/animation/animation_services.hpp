#pragma once

namespace engine
{
class AnimationRetargeter;
class AnimationSystem;
namespace animation_detail
{
// World-local借用，仅供组件的同步原子播放预检；生命周期由 AnimationSystem 管理。
struct AnimationServices
{
    AnimationSystem* owner = nullptr;
    AnimationRetargeter* retargeter = nullptr;
};
} // namespace animation_detail
} // namespace engine
