#pragma once

#include "runtime/resource/resource_handle.hpp"

#include "runtime/resource/resource.hpp"
#include "runtime/scene/component.hpp"

#include <cstdint>
#include <string_view>

namespace engine
{
class ResourceReference;
}

namespace engine
{
struct UiComponentRuntime;

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
    static constexpr ComponentType component_type = ComponentType::UiData;
    [[nodiscard]] static bool valid_settings(const schema::UiDataT& settings) noexcept;
    [[nodiscard]] bool get_settings(Data& output) const noexcept;
    [[nodiscard]] bool set_settings(const schema::UiDataT& settings);
    [[nodiscard]] bool set_document(std::string_view path);
    [[nodiscard]] ResourceHandle document() const noexcept;
    [[nodiscard]] std::uint64_t document_revision() const noexcept;
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data);

private:
    [[nodiscard]] bool apply_settings(const Data& settings, ResourceReference prepared);
    template <class T> friend auto engine::ecs_detail::component_runtime(T component) noexcept;
    [[nodiscard]] Runtime* runtime() const noexcept;
};
} // namespace engine
