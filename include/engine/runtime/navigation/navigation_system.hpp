#pragma once

#include "runtime/system/system.hpp"

struct rcHeightfield;
class dtNavMesh;

namespace engine
{

class NavigationSystem final : public System
{
public:
    NavigationSystem() noexcept;
    ~NavigationSystem() noexcept override;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    void on_shutdown() noexcept override;

private:
    rcHeightfield* heightfield_ = nullptr;
    dtNavMesh* nav_mesh_ = nullptr;
};

} // namespace engine
