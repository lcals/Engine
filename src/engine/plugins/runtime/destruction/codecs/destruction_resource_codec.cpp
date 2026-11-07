#include "plugins/runtime/destruction/codecs/destruction_resource_codec.hpp"
#include "plugins/runtime/destruction/destruction_graph.hpp"

#include <flatbuffers/verifier.h>

namespace engine::plugins::destruction
{
schema::AssetKind DestructionResourceCodec::kind() const noexcept
{
    return schema::AssetKind::destruction;
}
bool DestructionResourceCodec::decode(const schema::AssetEntryT& entry, ByteView payload,
                                      std::unique_ptr<Resource>& output, std::string& error) const
{
    error.clear();
    if (output || entry.kind != kind() || payload.empty() || payload.size() > 64U * 1024U * 1024U)
    {
        error = "Invalid resource output, kind or payload size";
        return false;
    }
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
    flatbuffers::Verifier verifier(bytes, payload.size());
    if (!schema::VerifyDestructionAssetDataBuffer(verifier))
    {
        error = "Invalid Destruction FlatBuffer";
        return false;
    }
    auto data = schema::UnPackDestructionAssetData(bytes);
    output = std::make_unique<ResourceType>(entry, std::move(*data));
    return true;
}
bool DestructionResourceCodec::validate_data(const Resource& resource, std::string& error) const
{
    error.clear();
    const auto* data = resource_detail::data<ResourceType>(&resource);
    if (!data || !resource.info().dependencies.empty())
    {
        error = "Invalid destruction type or dependency declaration";
        return false;
    }
    return DestructionGraph::validate(*data, error);
}
bool DestructionResourceCodec::validate_dependencies(const Resource& resource,
                                                     ResourceView dependencies,
                                                     std::string& error) const
{
    error.clear();
    return resource.info().dependencies.empty() && dependencies.empty();
}
} // namespace engine::plugins::destruction
