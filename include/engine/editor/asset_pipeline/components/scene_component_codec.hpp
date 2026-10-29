#pragma once

#include "runtime/schema/scene_generated.h"
#include <toml++/toml.hpp>

#include <memory>
#include <string>
#include <string_view>

namespace engine::editor::asset_pipeline
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

// 登记成功转移所有权；类型或名字冲突时保留输入，不支持运行中卸载代码。
[[nodiscard]] bool register_scene_component_codec(std::unique_ptr<SceneComponentCodec>& codec);

[[nodiscard]] const SceneComponentCodec*
find_scene_component_codec(schema::ComponentPayload type) noexcept;
[[nodiscard]] const SceneComponentCodec* find_scene_component_codec(std::string_view name) noexcept;

} // namespace engine::editor::asset_pipeline
