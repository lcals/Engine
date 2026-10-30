#pragma once

#include "runtime/resource/resource.hpp"
#include "runtime/scene/component.hpp"

#include <cstdint>
#include <string_view>

namespace engine
{
class UiComponent final : public Component
{
public:
    static constexpr ComponentType component_type = ComponentType::UiData;
    [[nodiscard]] static bool valid_settings(const schema::UiDataT& settings) noexcept;
    [[nodiscard]] const schema::UiDataT& settings() const noexcept;
    [[nodiscard]] bool set_settings(const schema::UiDataT& settings);
    [[nodiscard]] bool set_document(std::string_view path);
    [[nodiscard]] const ResourcePtr& document() const noexcept;
    [[nodiscard]] std::uint64_t view_id() const noexcept;
    [[nodiscard]] std::uint64_t document_revision() const noexcept;
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data) override;

private:
    friend class Node;
    explicit UiComponent(Node& node);
    [[nodiscard]] bool initialize() override;
    std::uint64_t view_id_ = 0;
    std::uint64_t document_revision_ = 0;
    ResourcePtr document_;
};
} // namespace engine
