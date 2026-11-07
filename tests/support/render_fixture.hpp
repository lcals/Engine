#pragma once
#include "runtime/render/render_system.hpp"

namespace test_support
{
inline bool prepare_render(engine::RenderSystem& renderer, engine::Camera camera = {})
{
    engine::RenderPrepareProgress progress;
    return renderer.prepare_scene(engine::RenderPrepareMode::synchronous, progress, 0, camera) ==
           engine::RenderPrepareStatus::ready;
}
inline bool render_frame(engine::RenderSystem& renderer, engine::Camera camera = {})
{
    return prepare_render(renderer, camera) && renderer.render(camera);
}
inline bool render_ready(const engine::RenderSystem& renderer)
{
    engine::RenderPrepareProgress progress;
    return renderer.preparation_status(progress) == engine::RenderPrepareStatus::ready;
}
} // namespace test_support
