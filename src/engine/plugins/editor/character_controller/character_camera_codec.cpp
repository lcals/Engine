#include "plugins/editor/character_controller/character_camera_codec.hpp"
#include "editor/asset_pipeline/control_toml.hpp"
#include "plugins/runtime/character_controller/character_camera.hpp"
namespace engine::plugins::character_controller
{
schema::ComponentPayload CharacterCameraCodec::type() const noexcept
{
    return schema::ComponentPayload::CharacterCameraData;
}
std::string_view CharacterCameraCodec::name() const noexcept
{
    return "character_camera";
}
bool CharacterCameraCodec::validate(const schema::ComponentDataT& c, std::string& e) const
{
    const auto* d = c.payload.AsCharacterCameraData();
    if (!d)
    {
        e = "invalid character_camera payload";
        return false;
    }
    return CharacterCamera::valid_settings(*d) || (e = "invalid character camera settings", false);
}
bool CharacterCameraCodec::read(const toml::table& t, schema::ComponentDataT& c,
                                std::string& e) const
{
    nlohmann::json j;
    schema::CharacterCameraDataT d;
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
void CharacterCameraCodec::write(const schema::ComponentDataT& c, toml::table& t) const
{
    const auto* d = c.payload.AsCharacterCameraData();
    if (!d)
    {
        return;
    }
    t.insert_or_assign("type", "character_camera");
    editor::asset_pipeline::control_toml::write_table(schema::control_json::write(*d), t);
}
bool register_character_camera_codec()
{
    if (!editor::asset_pipeline::find_scene_component_codec(
            schema::ComponentPayload::CharacterCameraData))
    {
        std::unique_ptr<editor::asset_pipeline::SceneComponentCodec> codec =
            std::make_unique<CharacterCameraCodec>();
        if (!editor::asset_pipeline::register_scene_component_codec(codec))
        {
            return false;
        }
    }
    return true;
}
} // namespace engine::plugins::character_controller
