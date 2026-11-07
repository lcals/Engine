#pragma once

#include "plugins/runtime/effects/effect_resource_path.hpp"
#include "runtime/render/resources/gpu_texture.hpp"

#include <bgfxrenderer.h>

#include <map>
#include <string>

namespace engine::plugins::effects::detail
{

// 比 SDK renderer 与其所有 effect 活得更久，保存上游回调借用的 GPU 对象。
class EffectResources final
{
public:
    EffectResources() = default;
    ~EffectResources() noexcept;
    EffectResources(const EffectResources&) = delete;
    EffectResources& operator=(const EffectResources&) = delete;

    [[nodiscard]] EffekseerRendererBGFX::InitArgs renderer_arguments(std::uint16_t view_id);
    // 只在官方 Effect::Create 的同步加载回调期间借用；完成后立即清空。
    const EffectBindings* loading = nullptr;
    [[nodiscard]] bool resolve(std::string_view path, schema::AssetKind kind,
                               resource_detail::ResourceReference& output);
    void release_textures() noexcept;
    void release_shaders() noexcept;
    std::string error;

private:
    struct TextureEntry
    {
        render_detail::GpuTexture gpu;
        std::uint32_t references = 1;
    };

    static bgfx_shader_handle_t load_shader(const char* material, const char* name,
                                            const char* stage, void* context);
    static int load_texture(const char* name, int srgb, void* context);
    static void unload_texture(int id, void* context);
    static bgfx_texture_handle_t texture_handle(int id, void* context);

    std::map<int, TextureEntry> textures_;
    std::map<std::string, bgfx::ShaderHandle, std::less<>> shaders_;
    int next_texture_ = 0;
};

} // namespace engine::plugins::effects::detail
