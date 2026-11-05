#pragma once

#include "runtime/scene/scene_types.hpp"

#include <array>
#include <cstdint>

namespace engine
{
class Scene;
class Node;
namespace ecs_detail
{
struct Published
{
};
struct SceneMember
{
};

struct NodeRuntime
{
    Scene* scene = nullptr;
    std::uint64_t order = 0;
    std::uint64_t prefab_order = 0;
    std::uint64_t prepared_revision = 0;
    bool transient = false;
    bool persistent = false;
    bool active = true;
    bool visible_in_hierarchy = true;
};

struct TransformDrive
{
    ComponentType owner_type = ComponentType::NONE;
    std::uint64_t owner_instance_id = 0;
    // Borrowed producer code; must remain valid and only inspect current ECS state.
    bool (*validate)(Node owner, std::uint64_t owner_instance_id) noexcept = nullptr;
    std::array<float, 16> local{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    bool active = false;
};

struct WorldTransform
{
    std::array<float, 16> matrix{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    bool valid = true;
};
} // namespace ecs_detail
} // namespace engine
