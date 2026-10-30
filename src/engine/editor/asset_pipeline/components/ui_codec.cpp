#include "editor/asset_pipeline/components/ui_codec.hpp"
#include "runtime/scene/ui_component.hpp"
namespace engine::editor::asset_pipeline
{
schema::ComponentPayload UiCodec::type() const noexcept
{
    return schema::ComponentPayload::UiData;
}
std::string_view UiCodec::name() const noexcept
{
    return "ui";
}
bool UiCodec::read(const toml::table& table, schema::ComponentDataT& component,
                   std::string& error) const
{
    schema::UiDataT data;
    if (table.contains("document"))
    {
        auto value = table["document"].value<std::string>();
        if (!value)
        {
            error = "ui.document must be a GUID string";
            return false;
        }
        data.document_guid = *value;
    }
    component.payload.Set(std::move(data));
    return validate(component, error);
}
bool UiCodec::validate(const schema::ComponentDataT& component, std::string& error) const
{
    const auto* data = component.payload.AsUiData();
    if (!data || !UiComponent::valid_settings(*data))
    {
        error = "invalid ui settings";
        return false;
    }
    return true;
}
void UiCodec::write(const schema::ComponentDataT& component, toml::table& table) const
{
    table.insert("type", "ui");
    table.insert("document", component.payload.AsUiData()->document_guid);
}
} // namespace engine::editor::asset_pipeline
