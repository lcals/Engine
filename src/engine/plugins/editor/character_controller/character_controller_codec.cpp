#include "plugins/editor/character_controller/character_controller_codec.hpp"
#include "editor/asset_pipeline/control_toml.hpp"
#include "plugins/editor/character_controller/character_camera_codec.hpp"
#include "plugins/editor/character_controller/character_visual_codec.hpp"
#include "plugins/runtime/character_controller/character_controller.hpp"
namespace engine::plugins::character_controller
{
schema::ComponentPayload CharacterControllerCodec::type() const noexcept
{
    return schema::ComponentPayload::CharacterControllerData;
}
std::string_view CharacterControllerCodec::name() const noexcept
{
    return "character_controller";
}
bool CharacterControllerCodec::validate(const schema::ComponentDataT& c, std::string& e) const
{
    const auto* d = c.payload.AsCharacterControllerData();
    if (!d)
    {
        e = "invalid character_controller payload";
        return false;
    }
    return CharacterController::valid_settings(*d, types_, e);
}
bool CharacterControllerCodec::read(const toml::table& t, schema::ComponentDataT& c,
                                    std::string& e) const
{
    nlohmann::json j;
    schema::CharacterControllerDataT d;
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
void CharacterControllerCodec::write(const schema::ComponentDataT& c, toml::table& t) const
{
    const auto* d = c.payload.AsCharacterControllerData();
    if (!d)
    {
        return;
    }
    t.insert_or_assign("type", "character_controller");
    editor::asset_pipeline::control_toml::write_table(schema::control_json::write(*d), t);
}
bool CharacterControllerCodec::remap_node_references(
    schema::ComponentDataT& c, std::span<const schema::NodeIdMappingT> mappings,
    std::string& e) const
{
    auto* d = c.payload.AsCharacterControllerData();
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
    return remap(d->camera_node_id);
}
bool register_controller_codec(const CharacterAbilityRegistry& types)
{
    if (!types.frozen())
    {
        return false;
    }
    if (!editor::asset_pipeline::find_scene_component_codec(
            schema::ComponentPayload::CharacterControllerData))
    {
        std::unique_ptr<editor::asset_pipeline::SceneComponentCodec> codec =
            std::make_unique<CharacterControllerCodec>(types);
        if (!editor::asset_pipeline::register_scene_component_codec(codec))
        {
            return false;
        }
    }
    return register_character_camera_codec() && register_character_visual_codec();
}
} // namespace engine::plugins::character_controller
