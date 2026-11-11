#pragma once
#include "runtime/render/render_system.hpp"
#include <SDL3/SDL.h>
#include <chrono>
#include <cstdio>

namespace test_support
{
inline bool prepare_render(engine::RenderSystem& renderer, engine::Camera camera = {})
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline)
    {
        const auto state = renderer.prepare_scope({}, {}, camera);
        if (state == engine::SceneWorkState::ready || state == engine::SceneWorkState::not_required)
        {
            return true;
        }
        if (state == engine::SceneWorkState::failed)
        {
            engine::ScenePreparationSnapshot snapshot;
            if (renderer.scope_status({}, snapshot))
            {
                for (const auto& stage : snapshot.stages)
                {
                    std::fprintf(stderr, "%s\n", stage.error.c_str());
                }
            }
            return false;
        }
        if (!(renderer.render(camera) == engine::RenderSubmitStatus::submitted))
        {
            return false;
        }
        SDL_Delay(1);
    }
    std::fprintf(stderr, "Scene preparation timed out\n");
    return false;
}
inline bool render_frame(engine::RenderSystem& renderer, engine::Camera camera = {})
{
    return prepare_render(renderer, camera) &&
           (renderer.render(camera) == engine::RenderSubmitStatus::submitted);
}
inline bool render_ready(const engine::RenderSystem& renderer)
{
    engine::ScenePreparationSnapshot snapshot;
    return renderer.scope_status({}, snapshot) &&
           (snapshot.state == engine::SceneWorkState::ready ||
            snapshot.state == engine::SceneWorkState::not_required);
}
} // namespace test_support
