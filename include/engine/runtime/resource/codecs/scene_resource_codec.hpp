#pragma once

#include "runtime/resource/resource_codec.hpp"

namespace engine
{

class SceneResourceCodec final : public ResourceCodec
{
public:
    [[nodiscard]] static bool
    validate_nodes(const ResourceCatalog& catalog, const Resource& resource,
                   const std::vector<std::unique_ptr<schema::NodeDataT>>& nodes);
    [[nodiscard]] static bool
    validate_node_dependencies(const ResourceCatalog& catalog,
                               const std::vector<std::unique_ptr<schema::NodeDataT>>& nodes,
                               std::span<const ResourcePtr> dependencies);
    [[nodiscard]] bool decode(const ResourceCatalog& catalog, ByteView payload,
                              Resource& output) const override;
    [[nodiscard]] bool
    validate_dependencies(const ResourceCatalog& catalog, const Resource& resource,
                          std::span<const ResourcePtr> dependencies) const override;
};

} // namespace engine
