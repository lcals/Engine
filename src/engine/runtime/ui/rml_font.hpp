#pragma once

#include "runtime/font/font_system.hpp"
#include <RmlUi/Core/FontEngineInterface.h>
#include <map>
#include <memory>

namespace engine::ui_detail
{
class RmlFont final : public Rml::FontEngineInterface
{
public:
    explicit RmlFont(FontSystem& fonts) noexcept;
    [[nodiscard]] bool activate(const std::string& key, const ResourcePtr& document);
    void prune(std::span<const std::string> keys);
    [[nodiscard]] bool resolve_texture(std::string_view key, std::string_view font_guid,
                                       std::string_view shader_guid, ResourcePtr& font,
                                       ResourcePtr& shader) const;
    Rml::FontFaceHandle GetFontFaceHandle(const Rml::String& family, Rml::Style::FontStyle style,
                                          Rml::Style::FontWeight weight, int size) override;
    const Rml::FontMetrics& GetFontMetrics(Rml::FontFaceHandle handle) override;
    int GetStringWidth(Rml::FontFaceHandle handle, Rml::StringView text,
                       const Rml::TextShapingContext& context,
                       Rml::Character prior = Rml::Character::Null) override;
    int GenerateString(Rml::RenderManager& manager, Rml::FontFaceHandle handle,
                       Rml::FontEffectsHandle effects, Rml::StringView text, Rml::Vector2f position,
                       Rml::ColourbPremultiplied colour, float opacity,
                       const Rml::TextShapingContext& context,
                       Rml::TexturedMeshList& meshes) override;
    void ReleaseFontResources() override;

private:
    struct Face
    {
        ResourcePtr resource;
        std::string document_key;
        std::string shader_guid;
        Rml::FontMetrics metrics{};
    };
    struct Binding
    {
        ResourcePtr font;
        ResourcePtr shader;
    };
    FontSystem& fonts_;
    using Bindings = std::map<std::string, Binding>;
    std::map<std::string, Bindings> documents_;
    const Bindings* active_ = nullptr;
    std::string active_key_;
    std::map<std::pair<std::string, int>, std::unique_ptr<Face>> faces_;
};
} // namespace engine::ui_detail
