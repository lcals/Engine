#pragma once

#include "runtime/resource/resource_reference.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/world/world_system.hpp"

namespace engine
{
struct WorldSystem::Scope
{
    SceneScopeId id;
    SceneLoadKind kind = SceneLoadKind::replace_scene;
    std::uint64_t destination = 0;
    std::unique_ptr<Scene> candidate;
    Scene* published_scene = nullptr;
    ResourceReference resource;
    std::size_t nodes = 0;
    std::size_t parents = 0;
    std::size_t mapped = 0;
    std::vector<Node> members;
    std::vector<Node> previous_nodes;
    std::vector<Node> retirement_roots;
    std::size_t previous_prepared = 0;
    SceneWorkProgress progress;
    std::uint64_t started_ns = 0;
    bool restored = true;
    bool sealed = false;
    bool published = false;
    bool active = false;
};
} // namespace engine
