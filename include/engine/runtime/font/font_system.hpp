#pragma once

#include "runtime/resource/resource.hpp"
#include "runtime/schema/font_generated.h"
#include "runtime/system/system.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace engine
{

class Scene;

struct FontVertex
{
    float position[3]{};
    // Little-endian RGBA bytes; RGB is already multiplied by alpha.
    std::uint32_t color = 0xffffffff;
    float curve_coordinate[2]{};
    float curve_range[2]{};
    // Outward local XY direction and inverse XY font scale, for pixel-size dilation.
    float dilation[4]{};
};

struct FontGeometry
{
    std::vector<FontVertex> vertices;
    std::vector<std::uint32_t> indices;
    float width = 0.0F;
    float height = 0.0F;
};

struct FontLayout
{
    float size = 16.0F;
    float letter_spacing = 0.0F;
    std::uint32_t color = 0xffffffff;
    bool y_down = true;
};

struct FontDraw
{
    std::uint16_t view_id = 0;
    std::array<float, 16> transform{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::array<std::uint16_t, 4> scissor{};
    bool use_scissor = false;
    bool depth_test = false;
    // HDR scene targets require linear premultiplied color; UI keeps display color.
    bool linear_output = false;
    // The caller supplies the shader retained with this font/document generation.
    ResourcePtr shader;
};

class FontSystem final : public System
{
public:
    FontSystem();
    ~FontSystem() noexcept override;

    // A failed load or layout leaves the output empty.
    [[nodiscard]] bool load_font(std::string_view path_or_guid, ResourcePtr& output);
    [[nodiscard]] static const schema::FontGlyphT* glyph(const schema::FontDataT& data,
                                                         std::uint32_t codepoint) noexcept;
    [[nodiscard]] static float kerning(const schema::FontDataT& data, std::uint32_t left,
                                       std::uint32_t right) noexcept;
    [[nodiscard]] static float measure(const schema::FontDataT& data, std::string_view text,
                                       float size, float letter_spacing = 0.0F) noexcept;
    [[nodiscard]] static bool layout(const Resource& font, std::string_view text,
                                     const FontLayout& settings, FontGeometry& output);
    // Appends one padded quad. x/y are its baseline origin, not its bounding-box origin.
    [[nodiscard]] static bool append_glyph(const schema::FontDataT& data,
                                           const schema::FontGlyphT& glyph, float x, float y,
                                           float size, std::uint32_t color, bool y_down,
                                           FontGeometry& output);
    [[nodiscard]] bool draw(const ResourcePtr& font, const FontGeometry& geometry,
                            const FontDraw& settings);
    // The caller has already configured this view's camera and viewport.
    [[nodiscard]] bool render_scene(const Scene& scene, std::uint16_t view_id,
                                    bool linear_output = false);
    void release_scene() noexcept;
    [[nodiscard]] std::uint32_t gpu_resource_count() const noexcept;
    [[nodiscard]] const std::string& last_error() const noexcept;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    void on_shutdown() noexcept override;

private:
    struct GpuState;
    std::unique_ptr<GpuState> gpu_;
    std::string last_error_;
};

} // namespace engine
