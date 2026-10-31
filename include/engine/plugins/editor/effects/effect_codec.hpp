#pragma once

#include "editor/asset_pipeline/components/scene_component_codec.hpp"

namespace engine::plugins::effects
{

class EffectCodec final : public editor::asset_pipeline::SceneComponentCodec
{
public:
    [[nodiscard]] schema::ComponentPayload type() const noexcept override;
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] bool read(const toml::table& table, schema::ComponentDataT& component,
                            std::string& error) const override;
    [[nodiscard]] bool validate(const schema::ComponentDataT& component,
                                std::string& error) const override;
    [[nodiscard]] bool scan_dependencies(const schema::ComponentDataT& component,
                                         const editor::asset_pipeline::ImportContext& context,
                                         std::vector<Guid>& dependencies,
                                         std::string& error) const override;
    void write(const schema::ComponentDataT& component, toml::table& table) const override;
};

[[nodiscard]] bool register_effect_codec();

} // namespace engine::plugins::effects
