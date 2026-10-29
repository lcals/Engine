#pragma once

#include "runtime/system/system.hpp"

#include <string>

namespace engine
{

class ProjectSystem final : public System
{
public:
    explicit ProjectSystem(std::string project_root);
    ~ProjectSystem() noexcept override;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    void on_shutdown() noexcept override;

private:
    std::string project_root_;
};

} // namespace engine
