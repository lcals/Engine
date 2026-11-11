#pragma once
#include "editor/asset_pipeline/components/scene_component_codec.hpp"
#include "plugins/runtime/camera/camera_type_registry.hpp"
namespace engine::plugins::camera_editor
{
class CameraControllerCodec final : public editor::asset_pipeline::SceneComponentCodec
{
public:
    explicit CameraControllerCodec(const camera::CameraTypeRegistry& types) : types_(types)
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
    const camera::CameraTypeRegistry& types_;
};
[[nodiscard]] bool register_camera_codec(const camera::CameraTypeRegistry& types);
} // namespace engine::plugins::camera_editor
