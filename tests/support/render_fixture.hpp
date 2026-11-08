#pragma once
#include "runtime/render/render_system.hpp"
#include <cstdio>

namespace test_support
{
inline bool prepare_render(engine::RenderSystem& renderer, engine::Camera camera = {})
{
    engine::RenderPrepareProgress progress;
    const bool ready = renderer.prepare_scene(engine::RenderPrepareMode::synchronous, progress, 0,
                                              camera) == engine::RenderPrepareStatus::ready;
    if (!ready)
    {
        const auto error = renderer.preparation_error();
        std::fprintf(stderr, "render preparation failed: %.*s\n", static_cast<int>(error.size()),
                     error.data());
    }
    return ready;
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
