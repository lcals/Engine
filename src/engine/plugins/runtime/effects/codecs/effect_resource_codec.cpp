#include "plugins/runtime/effects/codecs/effect_resource_codec.hpp"
#include "plugins/runtime/effects/effect_validation.hpp"
#include "runtime/core/guid_map.hpp"
#include "runtime/utils/path_utils.hpp"
#include <algorithm>
#include <set>

#include <flatbuffers/verifier.h>

namespace engine::plugins::effects
{
schema::AssetKind EffectResourceCodec::kind() const noexcept
{
    return schema::AssetKind::effect;
}
bool EffectResourceCodec::decode(const schema::AssetEntryT& entry, ByteView payload,
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
    if (!schema::VerifyEffectAssetDataBuffer(verifier))
    {
        error = "Invalid Effect FlatBuffer";
        return false;
    }
    auto data = schema::UnPackEffectAssetData(bytes);
    output = std::make_unique<ResourceType>(entry, std::move(*data));
    return true;
}
bool EffectResourceCodec::validate_data(const Resource& resource, std::string& error) const
{
    error.clear();
    const auto* data = resource_detail::data<ResourceType>(&resource);
    if (!data || data->data.size() > 64U * 1024U * 1024U ||
        !validate_effect(std::as_bytes(std::span(data->data)), error))
    {
        if (error.empty())
        {
            error = "Invalid effect data";
        }
        return false;
    }
    std::set<std::string_view> paths, dependencies;
    for (const auto& binding : data->bindings)
    {
        if (!binding)
        {
            error = "Missing effect binding";
            return false;
        }
        const auto& path = binding->path;
        const auto relative = PathUtils::from_utf8(path);
        if (path.empty() || relative.is_absolute() || relative.has_root_name() ||
            relative.filename().empty() || relative == "." || relative.filename() == ".." ||
            path != PathUtils::to_utf8(relative.lexically_normal()) ||
            std::any_of(path.begin(), path.end(), [](unsigned char value)
                        { return value < 32 || value == 127 || value == ':' || value == '\\'; }) ||
            !GuidMap::valid_guid(binding->guid) || !declares(resource, binding->guid) ||
            (binding->kind != schema::AssetKind::texture &&
             binding->kind != schema::AssetKind::source) ||
            !paths.insert(path).second)
        {
            error = "Invalid effect dependency binding";
            return false;
        }
        dependencies.insert(binding->guid);
    }
    return dependencies.size() == resource.info().dependencies.size();
}
bool EffectResourceCodec::validate_dependencies(const Resource& resource, ResourceView dependencies,
                                                std::string& error) const
{
    error.clear();
    const auto* data = resource_detail::data<ResourceType>(&resource);
    if (!data)
    {
        error = "Invalid effect type";
        return false;
    }
    for (const auto& binding : data->bindings)
    {
        const auto* value = dependency(dependencies, binding->guid);
        if (!value || value->kind() != binding->kind)
        {
            error = "Effect dependency type mismatch";
            return false;
        }
    }
    return true;
}
} // namespace engine::plugins::effects
