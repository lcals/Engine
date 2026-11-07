#include "runtime/resource/codecs/script_resource_codec.hpp"
#include "runtime/core/guid_map.hpp"
#include "runtime/resource/codecs/resource_bytes.hpp"

namespace engine
{
schema::AssetKind ScriptResourceCodec::kind() const noexcept
{
    return schema::AssetKind::script;
}
bool ScriptResourceCodec::decode(const schema::AssetEntryT& entry, ByteView payload,
                                 std::unique_ptr<Resource>& output, std::string& error) const
{
    error.clear();
    if (output || entry.kind != kind() || payload.size() > 64U * 1024U * 1024U)
    {
        error = "Invalid resource output, kind or payload size";
        return false;
    }
    output = std::make_unique<ResourceType>(entry, ByteBuffer(payload.begin(), payload.end()));
    return true;
}
bool ScriptResourceCodec::validate_data(const Resource& resource, std::string& error) const
{
    error.clear();
    const auto* data = resource_detail::data<ResourceType>(&resource);
    if (!data || data->size() > 64U * 1024U * 1024U ||
        !(resource_codec_detail::valid_utf8(*data) &&
          GuidMap::valid_path(resource.info().module_path) &&
          resource.info().module_path.find(":") == std::string::npos &&
          (resource.info().module_path.ends_with(".js") ||
           resource.info().module_path.ends_with(".mjs"))))
    {
        error = "Invalid script resource";
        return false;
    }
    return true;
}
bool ScriptResourceCodec::validate_dependencies(const Resource& resource, ResourceView dependencies,
                                                std::string& error) const
{
    error.clear();
    for (const auto* item : dependencies)
    {
        if (!item ||
            (item->kind() != schema::AssetKind::script && item->kind() != schema::AssetKind::json))
        {
            error = "Script dependency must be a script or JSON module";
            return false;
        }
    }
    return dependencies.size() == resource.info().dependencies.size();
}
} // namespace engine
