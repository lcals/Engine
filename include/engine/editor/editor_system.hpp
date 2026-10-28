#pragma once

#include "system.hpp"

struct ImGuiContext;

namespace engine
{

class EditorSystem final : public System
{
public:
    EditorSystem() noexcept;
    ~EditorSystem() noexcept override;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    void on_shutdown() noexcept override;

private:
    ImGuiContext* context_ = nullptr;
};

} // namespace engine
