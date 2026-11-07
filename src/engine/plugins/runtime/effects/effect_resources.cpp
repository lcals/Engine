#include "plugins/runtime/effects/effect_resources.hpp"

#include "effects_shaders.hpp"
#include "plugins/runtime/effects/effect_resource_path.hpp"
#include "runtime/render/texture_upload.hpp"
#include "runtime/resource/types/texture_resource.hpp"

#include <bgfx/bgfx.h>

#include <limits>
#include <utility>

namespace engine::plugins::effects::detail
{

EffectResources::~EffectResources() noexcept
{
    release_textures();
    release_shaders();
}

EffekseerRendererBGFX::InitArgs EffectResources::renderer_arguments(std::uint16_t view_id)
{
    EffekseerRendererBGFX::InitArgs arguments{};
    arguments.squareMaxCount = 16384;
    arguments.viewid = view_id;
    arguments.bgfx = bgfx_get_interface(BGFX_API_VERSION);
    arguments.shader_load = load_shader;
    arguments.texture_load = load_texture;
    arguments.texture_unload = unload_texture;
    arguments.texture_handle = texture_handle;
    arguments.ud = this;
    arguments.invz = false;
    return arguments;
}

bgfx_shader_handle_t EffectResources::load_shader(const char* material, const char* name,
                                                  const char* stage, void* context)
{
    auto& resources = *static_cast<EffectResources*>(context);
    if (material != nullptr || name == nullptr || stage == nullptr)
    {
        resources.error = "Effects support predefined shaders only";
        return {bgfx::kInvalidHandle};
    }
    const std::string key = std::string(name) + '/' + stage;
    const auto existing = resources.shaders_.find(key);
    if (existing != resources.shaders_.end())
    {
        return {existing->second.idx};
    }
    const auto bytes = effect_shader(name, stage);
    if (bytes.empty() || bytes.size() > std::numeric_limits<std::uint32_t>::max())
    {
        resources.error = "Unknown effect shader: " + key;
        return {bgfx::kInvalidHandle};
    }
    const auto shader =
        bgfx::createShader(bgfx::copy(bytes.data(), static_cast<std::uint32_t>(bytes.size())));
    if (!bgfx::isValid(shader))
    {
        resources.error = "Cannot create effect shader: " + key;
        return {bgfx::kInvalidHandle};
    }
    resources.shaders_.emplace(key, shader);
    return {shader.idx};
}

int EffectResources::load_texture(const char* name, int, void* context)
{
    auto& resources = *static_cast<EffectResources*>(context);
    ResourceReference resource;
    if (name == nullptr || !resources.resolve(name, schema::AssetKind::texture, resource))
    {
        return -1;
    }
    for (auto& [id, existing] : resources.textures_)
    {
        const auto& source = existing.resource;
        if (source && source->info().guid == resource->info().guid &&
            source->generation() == resource->generation())
        {
            if (existing.references == std::numeric_limits<std::uint32_t>::max())
            {
                resources.error = "Effect texture reference count overflow";
                return -1;
            }
            ++existing.references;
            return id;
        }
    }
    if (resources.next_texture_ == std::numeric_limits<int>::max())
    {
        resources.error = "Effect texture identity overflow";
        return -1;
    }
    TextureEntry entry;
    // 颜色空间只由资源 meta 决定，不使用旧适配器固定传入的 srgb=0。
    // SDK 的同步回调逐张加载，仍在 bgfx API 线程准备并上传。
    if (!resource.data<TextureResource>() || resource.data<TextureResource>()->cube_map ||
        !render::upload_texture(*resource.loader(), resource.handle(), entry.handle))
    {
        resources.error = "Cannot upload effect texture: " + resource->info().path;
        return -1;
    }
    const int id = resources.next_texture_++;
    entry.resource = std::move(resource);
    resources.textures_.emplace(id, std::move(entry));
    return id;
}

bool EffectResources::resolve(std::string_view path, schema::AssetKind kind,
                              ResourceReference& output)
{
    output.reset();
    if (loading != nullptr)
    {
        const auto found = loading->find(effect_dependency_path(path));
        if (found != loading->end() && found->second->info().kind == kind)
        {
            return output.retain(found->second);
        }
    }
    error = "Effect dependency is missing from its compiled bindings: " + std::string(path);
    return false;
}

void EffectResources::unload_texture(int id, void* context)
{
    auto& resources = *static_cast<EffectResources*>(context);
    const auto item = resources.textures_.find(id);
    if (item != resources.textures_.end() && --item->second.references == 0)
    {
        bgfx::destroy(item->second.handle);
        resources.textures_.erase(item);
    }
}

bgfx_texture_handle_t EffectResources::texture_handle(int id, void* context)
{
    auto& resources = *static_cast<EffectResources*>(context);
    const auto item = resources.textures_.find(id);
    return {item != resources.textures_.end() ? item->second.handle.idx : bgfx::kInvalidHandle};
}

void EffectResources::release_textures() noexcept
{
    for (const auto& [id, texture] : textures_)
    {
        (void)id;
        bgfx::destroy(texture.handle);
    }
    textures_.clear();
    next_texture_ = 0;
}

void EffectResources::release_shaders() noexcept
{
    for (const auto& [name, shader] : shaders_)
    {
        (void)name;
        bgfx::destroy(shader);
    }
    shaders_.clear();
}

} // namespace engine::plugins::effects::detail
