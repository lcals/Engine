#pragma once
#include "editor/asset_pipeline/components/scene_component_codec.hpp"
#include "plugins/runtime/character_controller/character_ability_registry.hpp"
namespace engine::plugins::character_controller
{
class CharacterControllerCodec final : public editor::asset_pipeline::SceneComponentCodec
{
public:
    explicit CharacterControllerCodec(const CharacterAbilityRegistry& types) : types_(types)
    {
    }
    [[nodiscard]] schema::ComponentPayload type() const noexcept override;
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] bool read(const toml::table&, schema::ComponentDataT&,
                            std::string&) const override;
    [[nodiscard]] bool validate(const schema::ComponentDataT&, std::string&) const override;
    void write(const schema::ComponentDataT&, toml::table&) const override;
    [[nodiscard]] bool remap_node_references(schema::ComponentDataT&,
                                             std::span<const schema::NodeIdMappingT>,
                                             std::string&) const override;

private:
    const CharacterAbilityRegistry& types_;
};
[[nodiscard]] bool register_controller_codec(const CharacterAbilityRegistry& types);
} // namespace engine::plugins::character_controller
