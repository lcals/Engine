#pragma once

#include "runtime/scene/scene_types.hpp"

#include <array>
#include <cstdint>

namespace engine
{
class Scene;
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

struct WorldTransform
{
    std::array<float, 16> matrix{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    bool valid = true;
};
} // namespace ecs_detail
} // namespace engine
