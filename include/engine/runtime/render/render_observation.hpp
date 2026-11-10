#pragma once
#include "runtime/scene/scene_loading.hpp"

namespace engine
{
enum class RenderFrameKind
{
    scene,
    movie,
    clear
};
struct RenderFrameSnapshot
{
    std::uint64_t device_generation = 0;
    std::uint64_t submission_id = 0;
    std::uint64_t world_epoch = 0;
    std::uint64_t scene_id = 0;
    std::uint64_t content_revision = 0;
    std::uint64_t submitted_at_ns = 0;
    RenderFrameKind kind = RenderFrameKind::clear;
    std::uint32_t mesh_draws = 0;
    std::uint32_t ui_draws = 0;
};
enum class ScreenshotSelection
{
    next_frame,
    scene_scope
};
enum class ScreenshotState
{
    waiting_frame,
    reading_back,
    writing,
    complete,
    failed,
    cancelled
};
struct ScreenshotId
{
    std::uint64_t owner = 0;
    std::uint64_t value = 0;
    auto operator<=>(const ScreenshotId&) const = default;
};
struct ScreenshotRequest
{
    std::string path;
    ScreenshotSelection selection = ScreenshotSelection::next_frame;
    SceneScopeId scope;
    std::uint64_t device_generation = 0;
    std::uint64_t after_submission_id = 0;
};
struct ScreenshotSnapshot
{
    ScreenshotId id;
    ScreenshotState state = ScreenshotState::waiting_frame;
    RenderFrameSnapshot frame;
    std::string path;
    std::string error_code;
    std::string error;
};
} // namespace engine
