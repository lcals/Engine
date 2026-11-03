#pragma once

#include "runtime/core/guid_map.hpp"
#include "runtime/schema/scene_generated.h"
#include <toml++/toml.hpp>

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace engine::editor::asset_pipeline
{

struct ImportContext;

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
    // 向本批次列表追加已校验的资源 GUID；默认不声明附加依赖。
    [[nodiscard]] virtual bool scan_dependencies(const schema::ComponentDataT& component,
                                                 const ImportContext& context,
                                                 std::vector<Guid>& dependencies,
                                                 std::string& error) const;
    // 明确声明资源属性名；覆盖扫描不把普通字符串猜成资源引用。
    [[nodiscard]] virtual std::span<const std::string_view> resource_fields() const noexcept;
    virtual void write(const schema::ComponentDataT& component, toml::table& value) const = 0;
    [[nodiscard]] virtual bool
    remap_node_references(schema::ComponentDataT& component,
                          std::span<const schema::NodeIdMappingT> mapping,
                          std::string& error) const;
};

// 登记成功转移所有权；类型或名字冲突时保留输入，不支持运行中卸载代码。
[[nodiscard]] bool register_scene_component_codec(std::unique_ptr<SceneComponentCodec>& codec);

[[nodiscard]] const SceneComponentCodec*
find_scene_component_codec(schema::ComponentPayload type) noexcept;
[[nodiscard]] const SceneComponentCodec* find_scene_component_codec(std::string_view name) noexcept;

} // namespace engine::editor::asset_pipeline
