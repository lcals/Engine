#pragma once
#include "editor/asset_pipeline/components/scene_component_codec.hpp"
namespace engine::plugins::character_controller
{
class CharacterCameraCodec final : public editor::asset_pipeline::SceneComponentCodec
{
public:
    [[nodiscard]] schema::ComponentPayload type() const noexcept override;
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] bool read(const toml::table&, schema::ComponentDataT&,
                            std::string&) const override;
    [[nodiscard]] bool validate(const schema::ComponentDataT&, std::string&) const override;
    void write(const schema::ComponentDataT&, toml::table&) const override;
};
[[nodiscard]] bool register_character_camera_codec();
} // namespace engine::plugins::character_controller
