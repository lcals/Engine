#pragma once

#include "editor/asset_pipeline/components/scene_component_codec.hpp"

namespace engine::plugins::destruction
{

class DestructionCodec final : public editor::asset_pipeline::SceneComponentCodec
{
public:
    [[nodiscard]] schema::ComponentPayload type() const noexcept override;
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] bool read(const toml::table& table, schema::ComponentDataT& component,
                            std::string& error) const override;
    [[nodiscard]] bool validate(const schema::ComponentDataT& component,
                                std::string& error) const override;
    [[nodiscard]] bool scan_dependencies(const schema::ComponentDataT& component,
                                         const editor::asset_pipeline::ImportContext& context,
                                         std::vector<Guid>& dependencies,
                                         std::string& error) const override;
    [[nodiscard]] std::span<const std::string_view> resource_fields() const noexcept override;
    void write(const schema::ComponentDataT& component, toml::table& table) const override;
};

[[nodiscard]] bool register_destruction_codec();

} // namespace engine::plugins::destruction
