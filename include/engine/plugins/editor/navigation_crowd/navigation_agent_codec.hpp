#pragma once

#include "editor/asset_pipeline/components/scene_component_codec.hpp"

namespace engine::plugins::navigation_crowd
{

class NavigationAgentCodec final : public editor::asset_pipeline::SceneComponentCodec
{
public:
    [[nodiscard]] schema::ComponentPayload type() const noexcept override;
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] bool read(const toml::table& table, schema::ComponentDataT& component,
                            std::string& error) const override;
    [[nodiscard]] bool validate(const schema::ComponentDataT& component,
                                std::string& error) const override;
    void write(const schema::ComponentDataT& component, toml::table& table) const override;
};

[[nodiscard]] bool register_navigation_agent_codec();

} // namespace engine::plugins::navigation_crowd
