#pragma once

#include "runtime/resource/resource_handle.hpp"

#include "runtime/scene/component.hpp"

#include <memory>
#include <string_view>

namespace engine
{

class Resource;

struct TextComponentRuntime;

class TextComponent final : public Component
{
public:
    using Data = schema::TextDataT;
    TextComponent() noexcept = default;
    explicit TextComponent(Component component) noexcept : Component(component, component_type)
    {
    }
    [[nodiscard]] static bool register_type(ComponentRegistry& registry);
    [[nodiscard]] bool initialize();
    using Runtime = TextComponentRuntime;
    static constexpr ComponentType component_type = ComponentType::TextData;
    [[nodiscard]] static bool valid_settings(const schema::TextDataT& settings) noexcept;
    [[nodiscard]] schema::TextDataT settings() const noexcept;
    [[nodiscard]] bool get_settings(Data& output) const noexcept;
    [[nodiscard]] bool set_settings(const schema::TextDataT& settings);
    [[nodiscard]] bool set_font(std::string_view path);
    [[nodiscard]] ResourceHandle font() const noexcept;
    [[nodiscard]] ResourceHandle shader() const noexcept;
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data);

private:
    template <class T> friend auto engine::ecs_detail::component_runtime(T component) noexcept;
    [[nodiscard]] Runtime* runtime() const noexcept;
};

} // namespace engine
