#pragma once

#include "runtime/scene/component.hpp"

#include <memory>
#include <string_view>

namespace engine
{

class Resource;

class TextComponent final : public Component
{
public:
    static constexpr ComponentType component_type = ComponentType::TextData;
    [[nodiscard]] static bool valid_settings(const schema::TextDataT& settings) noexcept;
    [[nodiscard]] const schema::TextDataT& settings() const noexcept;
    [[nodiscard]] bool set_settings(const schema::TextDataT& settings);
    [[nodiscard]] bool set_font(std::string_view path);
    [[nodiscard]] const std::shared_ptr<const Resource>& font() const noexcept;
    [[nodiscard]] const std::shared_ptr<const Resource>& shader() const noexcept;
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data) override;

private:
    friend class Node;
    explicit TextComponent(Node& node);

    std::shared_ptr<const Resource> font_;
    std::shared_ptr<const Resource> shader_;
};

} // namespace engine
