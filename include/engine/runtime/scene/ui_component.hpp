#pragma once

#include "runtime/resource/resource.hpp"
#include "runtime/scene/component.hpp"

#include <cstdint>
#include <string_view>

namespace engine
{
struct UiComponentRuntime
{
    std::uint64_t document_revision_ = 0;
    ResourcePtr document_;
};

class UiComponent final : public Component
{
public:
    using Data = schema::UiDataT;
    UiComponent() noexcept = default;
    explicit UiComponent(Component component) noexcept : Component(component, component_type)
    {
    }
    [[nodiscard]] static bool register_type(ComponentRegistry& registry);
    using Runtime = UiComponentRuntime;
    [[nodiscard]] Runtime* runtime() const noexcept;
    static constexpr ComponentType component_type = ComponentType::UiData;
    [[nodiscard]] static bool valid_settings(const schema::UiDataT& settings) noexcept;
    [[nodiscard]] schema::UiDataT settings() const noexcept;
    [[nodiscard]] bool get_settings(Data& output) const noexcept;
    [[nodiscard]] bool set_settings(const schema::UiDataT& settings);
    [[nodiscard]] bool set_document(std::string_view path);
    [[nodiscard]] ResourcePtr document() const noexcept;
    [[nodiscard]] std::uint64_t document_revision() const noexcept;
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data);
};
} // namespace engine
