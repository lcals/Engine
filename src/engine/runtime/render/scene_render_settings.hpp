#pragma once

#include "runtime/render/render_settings.hpp"

namespace engine
{

class EnvironmentComponent;
class Light;
class PostProcessComponent;
class Scene;

namespace render_detail
{

// 仅在当前主线程调用期间借用；不缓存跨帧或跨场景的组件指针。
struct SceneRenderComponents
{
    EnvironmentComponent* environment = nullptr;
    PostProcessComponent* post_process = nullptr;
    Light* shadow_light = nullptr;
};

[[nodiscard]] bool scene_render_components(Scene& scene, SceneRenderComponents& output);
[[nodiscard]] RenderSettings render_settings(const SceneRenderComponents& components);

} // namespace render_detail
} // namespace engine
