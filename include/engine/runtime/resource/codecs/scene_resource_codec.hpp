#pragma once

#include "runtime/resource/resource_codec.hpp"
#include "runtime/resource/types/scene_resource.hpp"

namespace engine
{

class SceneResourceCodec final : public ResourceCodec
{
public:
    using ResourceType = SceneResource;
    [[nodiscard]] schema::AssetKind kind() const noexcept override;
    [[nodiscard]] bool decode(const schema::AssetEntryT& entry, ByteView payload,
                              std::unique_ptr<Resource>& output, std::string& error) const override;
    [[nodiscard]] bool validate_data(const Resource& resource, std::string& error) const override;
    [[nodiscard]] bool validate_dependencies(const Resource& resource, ResourceView dependencies,
                                             std::string& error) const override;

    [[nodiscard]] static bool
    validate_nodes(const Resource& resource,
                   const std::vector<std::unique_ptr<schema::NodeDataT>>& nodes);
    [[nodiscard]] static bool
    validate_node_dependencies(const std::vector<std::unique_ptr<schema::NodeDataT>>& nodes,
                               ResourceView dependencies);
};

} // namespace engine
