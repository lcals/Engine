#pragma once

#include "editor/asset_pipeline/toml_helpers.hpp"
#include "runtime/schema/scene_generated.h"

#include <string>
#include <string_view>

namespace engine::asset_pipeline
{

class SceneComponentCodec
{
public:
    virtual ~SceneComponentCodec() noexcept = default;
    [[nodiscard]] virtual schema::ComponentPayload type() const noexcept = 0;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual bool read(const toml::table& table, schema::ComponentDataT& component,
                                    std::string& error) const = 0;
    [[nodiscard]] virtual bool validate(const schema::ComponentDataT& component,
                                        std::string& error) const = 0;
    virtual void write(const schema::ComponentDataT& component, toml::table& value) const = 0;
};

[[nodiscard]] const SceneComponentCodec*
find_scene_component_codec(schema::ComponentPayload type) noexcept;
[[nodiscard]] const SceneComponentCodec* find_scene_component_codec(std::string_view name) noexcept;

} // namespace engine::asset_pipeline
