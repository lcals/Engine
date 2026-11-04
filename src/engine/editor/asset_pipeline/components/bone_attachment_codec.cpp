#include "editor/asset_pipeline/components/bone_attachment_codec.hpp"

#include "editor/asset_pipeline/toml_helpers.hpp"
#include "runtime/scene/bone_attachment.hpp"

namespace engine::editor::asset_pipeline
{
schema::ComponentPayload BoneAttachmentCodec::type() const noexcept
{
    return schema::ComponentPayload::BoneAttachmentData;
}
std::string_view BoneAttachmentCodec::name() const noexcept
{
    return "bone_attachment";
}
bool BoneAttachmentCodec::validate(const schema::ComponentDataT& component,
                                   std::string& error) const
{
    const auto* value = component.payload.AsBoneAttachmentData();
    if (!value || !BoneAttachment::valid_settings(*value))
    {
        error = "invalid bone_attachment settings";
        return false;
    }
    return true;
}
bool BoneAttachmentCodec::read(const toml::table& table, schema::ComponentDataT& component,
                               std::string& error) const
{
    schema::BoneAttachmentDataT value;
    std::array<double, 3> translation{0, 0, 0}, scale{1, 1, 1};
    std::array<double, 4> rotation{0, 0, 0, 1};
    if ((table.contains("source_node_id") &&
         !identifier(table.get("source_node_id"), value.source_node_id)) ||
        (table.contains("translation") && !numbers(table.get("translation"), translation)) ||
        (table.contains("rotation") && !numbers(table.get("rotation"), rotation)) ||
        (table.contains("scale") && !numbers(table.get("scale"), scale)))
    {
        error = "invalid bone_attachment source or offset";
        return false;
    }
    if (table.contains("socket_name"))
    {
        const auto name = table["socket_name"].value<std::string>();
        if (!name)
        {
            error = "bone_attachment.socket_name must be string";
            return false;
        }
        value.socket_name = *name;
    }
    if (table.contains("follow_enabled"))
    {
        const auto enabled = table["follow_enabled"].value<bool>();
        if (!enabled)
        {
            error = "bone_attachment.follow_enabled must be boolean";
            return false;
        }
        value.follow_enabled = *enabled;
    }
    value.offset =
        schema::JointLocalPose(schema::Vec3(translation[0], translation[1], translation[2]),
                               schema::Vec4(rotation[0], rotation[1], rotation[2], rotation[3]),
                               schema::Vec3(scale[0], scale[1], scale[2]));
    component.payload.Set(std::move(value));
    return validate(component, error);
}
void BoneAttachmentCodec::write(const schema::ComponentDataT& component, toml::table& table) const
{
    const auto* value = component.payload.AsBoneAttachmentData();
    table.insert("type", "bone_attachment");
    table.insert("source_node_id", std::to_string(value->source_node_id));
    table.insert("socket_name", value->socket_name);
    table.insert("follow_enabled", value->follow_enabled);
    const auto& p = value->offset.translation();
    const auto& r = value->offset.rotation();
    const auto& s = value->offset.scale();
    table.insert("translation", toml::array{p.x(), p.y(), p.z()});
    table.insert("rotation", toml::array{r.x(), r.y(), r.z(), r.w()});
    table.insert("scale", toml::array{s.x(), s.y(), s.z()});
}
} // namespace engine::editor::asset_pipeline
