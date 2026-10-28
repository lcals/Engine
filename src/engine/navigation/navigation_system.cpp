#include "navigation_system.hpp"

#include <recastnavigation/DetourNavMesh.h>
#include <recastnavigation/Recast.h>

namespace engine
{

NavigationSystem::NavigationSystem() noexcept = default;

NavigationSystem::~NavigationSystem() noexcept
{
    shutdown();
}

bool NavigationSystem::on_initialize(const EngineConfig&)
{
    heightfield_ = rcAllocHeightfield();
    if (heightfield_ == nullptr)
    {
        return false;
    }
    nav_mesh_ = dtAllocNavMesh();
    return nav_mesh_ != nullptr;
}

void NavigationSystem::on_shutdown() noexcept
{
    dtFreeNavMesh(nav_mesh_);
    nav_mesh_ = nullptr;
    rcFreeHeightField(heightfield_);
    heightfield_ = nullptr;
}

} // namespace engine
