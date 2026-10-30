#pragma once

#include "runtime/font/font_system.hpp"
#include <RmlUi/Core/RenderInterface.h>
#include <bgfx/bgfx.h>
#include <map>

namespace engine::ui_detail
{
class RmlFont;
class RmlRender final : public Rml::RenderInterface
{
public:
    RmlRender(FontSystem& fonts, RmlFont& font_engine) noexcept;
    ~RmlRender() override;
    [[nodiscard]] bool begin(std::uint32_t width, std::uint32_t height, int logical_width,
                             int logical_height);
    void release() noexcept;
    [[nodiscard]] bool activate(const std::string& key, const ResourcePtr& document);
    void prune(std::span<const std::string> keys);
    [[nodiscard]] bool texture_guid(std::string_view key, std::string_view path,
                                    std::string& guid) const;
    [[nodiscard]] bool healthy() const noexcept;
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                Rml::Span<const int> indices) override;
    void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
                        Rml::TextureHandle texture) override;
    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;
    Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) override;
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source,
                                       Rml::Vector2i dimensions) override;
    void ReleaseTexture(Rml::TextureHandle texture) override;
    void EnableScissorRegion(bool enable) override;
    void SetScissorRegion(Rml::Rectanglei region) override;
    void SetTransform(const Rml::Matrix4f* transform) override;

private:
    struct Geometry
    {
        std::vector<Rml::Vertex> vertices;
        std::vector<std::uint32_t> indices;
    };
    struct Texture
    {
        ResourcePtr resource;
        ResourcePtr shader;
        bgfx::TextureHandle gpu = BGFX_INVALID_HANDLE;
        float font_size = 0;
        std::string document_key;
    };
    struct Document
    {
        std::map<std::string, ResourcePtr, std::less<>> textures;
    };
    FontSystem& fonts_;
    RmlFont& font_engine_;
    std::map<std::string, Document, std::less<>> documents_;
    std::map<Rml::CompiledGeometryHandle, Geometry> geometries_;
    std::map<Rml::TextureHandle, Texture> textures_;
    std::uintptr_t next_geometry_ = 1;
    std::uintptr_t next_texture_ = 1;
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sampler_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle white_ = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout layout_;
    std::array<float, 16> transform_{};
    std::array<std::uint16_t, 4> scissor_{};
    float scale_x_ = 1;
    float scale_y_ = 1;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::string active_key_;
    bool scissor_enabled_ = false;
    bool healthy_ = true;
};
} // namespace engine::ui_detail
