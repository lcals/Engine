#pragma once

#include "runtime/scene/camera.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace engine
{
struct SceneLoadId
{
    std::uint64_t owner = 0;
    std::uint64_t request = 0;
    auto operator<=>(const SceneLoadId&) const = default;
};

struct SceneScopeId
{
    std::uint64_t world_epoch = 0;
    std::uint64_t value = 0;
    auto operator<=>(const SceneScopeId&) const = default;
};

enum class SceneLoadKind
{
    replace_scene,
    append_batch
};
enum class SceneLoadState
{
    loading,
    staged,
    preparing,
    ready,
    activating,
    active,
    failed,
    cancelled
};
enum class SceneWorkState
{
    queued,
    working,
    ready,
    not_required,
    failed,
    cancelled
};

struct SceneWorkBudget
{
    std::uint32_t items = 64;
    double seconds = 0.002;
    std::uint64_t upload_bytes = 8 * 1024 * 1024;
};

struct SceneWorkProgress
{
    std::string domain;
    std::string stage;
    SceneWorkState state = SceneWorkState::queued;
    std::uint64_t completed = 0;
    std::uint64_t total = 0;
    bool total_known = false;
    std::uint64_t in_flight = 0;
    std::uint64_t progress_revision = 0;
    std::uint64_t last_progress_ns = 0;
    std::uint64_t elapsed_ns = 0;
    std::string error_code;
    std::string error;
};

struct ScenePreparationSnapshot
{
    SceneScopeId scope;
    SceneWorkState state = SceneWorkState::queued;
    std::vector<SceneWorkProgress> stages;
    std::uint64_t staging_bytes = 0;
};

struct SceneLoadSpec
{
    SceneLoadKind kind = SceneLoadKind::replace_scene;
    std::string scene_path;
    std::uint64_t destination_scene_id = 0;
    std::vector<std::string> preload_roots;
    Camera view_camera;
};

struct SceneLoadSnapshot
{
    SceneLoadId id;
    SceneScopeId scope;
    SceneLoadState state = SceneLoadState::loading;
    std::uint64_t scene_id = 0;
    bool target_alive = false;
    std::uint64_t progress_revision = 0;
    std::uint64_t last_progress_ns = 0;
    std::vector<SceneWorkProgress> stages;
    std::uint64_t first_scene_submission_id = 0;
    std::string error_code;
    std::string error;
};
} // namespace engine
