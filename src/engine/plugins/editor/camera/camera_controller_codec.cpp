#include "plugins/editor/camera/camera_controller_codec.hpp"
#include "editor/asset_pipeline/control_toml.hpp"
#include "plugins/runtime/camera/camera_controller.hpp"
namespace engine::plugins::camera_editor
{
schema::ComponentPayload CameraControllerCodec::type() const noexcept
{
    return schema::ComponentPayload::CameraControllerData;
}
std::string_view CameraControllerCodec::name() const noexcept
{
    return "camera_controller";
}
bool CameraControllerCodec::validate(const schema::ComponentDataT& c, std::string& e) const
{
    const auto* d = c.payload.AsCameraControllerData();
    if (!d)
    {
        e = "invalid camera_controller payload";
        return false;
    }
    return camera::CameraController::valid_settings(*d, types_, e);
}
bool CameraControllerCodec::read(const toml::table& t, schema::ComponentDataT& c,
                                 std::string& e) const
{
    nlohmann::json j;
    schema::CameraControllerDataT d;
    if (!editor::asset_pipeline::control_toml::read(t, j, e) ||
        !schema::control_json::read(j, d, e))
    {
        return false;
    }
    schema::ComponentDataT candidate;
    candidate.payload.Set(std::move(d));
    if (!validate(candidate, e))
    {
        return false;
    }
    c.payload = std::move(candidate.payload);
    return true;
}
void CameraControllerCodec::write(const schema::ComponentDataT& c, toml::table& t) const
{
    const auto* d = c.payload.AsCameraControllerData();
    if (!d)
    {
        return;
    }
    t.insert_or_assign("type", "camera_controller");
    editor::asset_pipeline::control_toml::write_table(schema::control_json::write(*d), t);
}
bool CameraControllerCodec::remap_node_references(schema::ComponentDataT& c,
                                                  std::span<const schema::NodeIdMappingT> mappings,
                                                  std::string& e) const
{
    auto* d = c.payload.AsCameraControllerData();
    if (!d)
    {
        return false;
    }
    const auto remap = [&](std::uint64_t& id)
    {
        if (!id)
        {
            return true;
        }
        for (const auto& m : mappings)
        {
            if (m.source_id == id)
            {
                if (!m.target_id || m.retired)
                {
                    break;
                }
                id = m.target_id;
                return true;
            }
        }
        e = "unmapped control node reference";
        return false;
    };
    auto targets = d->targets;
    for (auto& target : targets)
    {
        if (!remap(target.node_local_id))
        {
            return false;
        }
    }
    d->targets = std::move(targets);
    return true;
}
bool register_camera_codec(const camera::CameraTypeRegistry& types)
{
    if (!types.frozen())
    {
        return false;
    }
    if (!editor::asset_pipeline::find_scene_component_codec(
            schema::ComponentPayload::CameraControllerData))
    {
        std::unique_ptr<editor::asset_pipeline::SceneComponentCodec> codec =
            std::make_unique<CameraControllerCodec>(types);
        if (!editor::asset_pipeline::register_scene_component_codec(codec))
        {
            return false;
        }
    }
    return true;
}
} // namespace engine::plugins::camera_editor
