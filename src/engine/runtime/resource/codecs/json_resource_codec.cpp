#include "runtime/resource/codecs/json_resource_codec.hpp"
#include <nlohmann/json.hpp>

namespace engine
{
schema::AssetKind JsonResourceCodec::kind() const noexcept
{
    return schema::AssetKind::json;
}
bool JsonResourceCodec::decode(const schema::AssetEntryT& entry, ByteView payload,
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
bool JsonResourceCodec::validate_data(const Resource& resource, std::string& error) const
{
    error.clear();
    const auto* data = resource_detail::data<ResourceType>(&resource);
    if (!data || data->size() > 64U * 1024U * 1024U ||
        !(resource.info().dependencies.empty() && !data->empty() &&
          nlohmann::json::accept(reinterpret_cast<const char*>(data->data()),
                                 reinterpret_cast<const char*>(data->data()) + data->size())))
    {
        error = "Invalid json resource";
        return false;
    }
    return true;
}
bool JsonResourceCodec::validate_dependencies(const Resource& resource, ResourceView dependencies,
                                              std::string& error) const
{
    error.clear();
    return resource.info().dependencies.empty() && dependencies.empty();
}
} // namespace engine
