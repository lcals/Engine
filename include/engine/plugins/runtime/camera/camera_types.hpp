#pragma once

#include "runtime/scene/camera.hpp"
#include <array>
#include <span>
#include <string>
#include <vector>

namespace engine
{
class PhysicsSystem;
}
namespace engine::plugins::camera
{
enum class CameraRunState
{
    inactive,
    waiting,
    ready,
    blending,
    blocked,
    error
};
enum class CameraModuleStage
{
    lens,
    position,
    aim,
    modifier,
    constraint
};
struct CameraPose
{
    Vec3 position{};
    std::array<float, 4> rotation_xyzw{0, 0, 0, 1};
    CameraSettings lens{};
};
struct CameraStatus
{
    CameraRunState run_state = CameraRunState::inactive;
    bool has_output = false;
    std::string state, source_state, rig;
    double state_time = 0;
    float blend_progress = 0;
    CameraPose output;
    std::string diagnostic;
};
struct CameraTargetPose
{
    Node node;
    Vec3 position;
    std::array<float, 4> rotation_xyzw{0, 0, 0, 1};
};
struct CameraConditionMatch
{
    bool matched = false;
    std::vector<std::string> read_triggers;
};
struct CameraEvaluationContext
{
    double delta_time = 0;
    double state_time = 0;
    bool cut = false;
    const PhysicsSystem* physics = nullptr;
    std::span<const schema::CameraParameterValueT> parameters;
    std::span<const schema::CameraTargetDataT> target_slots;
    std::span<const CameraTargetPose> targets;
    CameraPose previous;
    std::span<const std::size_t> outgoing_transitions;
    [[nodiscard]] bool parameter(std::string_view name,
                                 schema::CameraParameterValueT& output) const;
    [[nodiscard]] bool target(std::string_view slot, CameraTargetPose& output) const;
};
struct CameraMachineRuntime
{
    std::string state_id, source_state_id, transition_id;
    std::string state_scope, source_scope;
    std::uint64_t entry_sequence = 0;
    double state_seconds = 0, blend_seconds = 0, blend_duration = 0;
    CameraPose source_snapshot;
};
struct CameraTransitionRequest
{
    bool selected = false;
    std::string transition_id, from, to;
    std::vector<std::string> consumed_triggers;
};
} // namespace engine::plugins::camera
