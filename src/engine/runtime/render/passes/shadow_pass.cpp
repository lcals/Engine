#include "runtime/render/passes/shadow_pass.hpp"

#include "runtime/render/render_math.hpp"
#include "runtime/scene/material.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/schema/shader_generated.h"

#include "fs_shadow.bin.h"
#include "vs_shadow.bin.h"

#include <bx/math.h>

#include <algorithm>
#include <cmath>

namespace engine::render_detail
{

ShadowPass::~ShadowPass() noexcept
{
    release();
}

bgfx::TextureHandle ShadowPass::texture() const noexcept
{
    return color_;
}

void ShadowPass::release() noexcept
{
    if (bgfx::isValid(framebuffer_))
    {
        for (std::uint16_t index = 0; index < maximum_shadow_cascades; ++index)
        {
            bgfx::setViewFrameBuffer(shadow_view + index, BGFX_INVALID_HANDLE);
        }
        bgfx::destroy(framebuffer_);
    }
    if (bgfx::isValid(color_))
    {
        bgfx::destroy(color_);
    }
    if (bgfx::isValid(depth_))
    {
        bgfx::destroy(depth_);
    }
    if (bgfx::isValid(program_))
    {
        bgfx::destroy(program_);
    }
    if (bgfx::isValid(vertex_))
    {
        bgfx::destroy(vertex_);
    }
    if (bgfx::isValid(fragment_))
    {
        bgfx::destroy(fragment_);
    }
    if (bgfx::isValid(alpha_))
    {
        bgfx::destroy(alpha_);
    }
    if (bgfx::isValid(alpha_sampler_))
    {
        bgfx::destroy(alpha_sampler_);
    }
    if (bgfx::isValid(uv_transform_))
    {
        bgfx::destroy(uv_transform_);
    }
    color_ = BGFX_INVALID_HANDLE;
    depth_ = BGFX_INVALID_HANDLE;
    framebuffer_ = BGFX_INVALID_HANDLE;
    vertex_ = BGFX_INVALID_HANDLE;
    fragment_ = BGFX_INVALID_HANDLE;
    program_ = BGFX_INVALID_HANDLE;
    alpha_ = BGFX_INVALID_HANDLE;
    alpha_sampler_ = BGFX_INVALID_HANDLE;
    uv_transform_ = BGFX_INVALID_HANDLE;
    resolution_ = 0;
}

bool ShadowPass::prepare(std::uint16_t size)
{
    if (resolution_ == size && bgfx::isValid(framebuffer_) && bgfx::isValid(program_))
    {
        return true;
    }
    release();
    constexpr auto color_flags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
                                 BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT |
                                 BGFX_SAMPLER_MIP_POINT;
    constexpr auto depth_flags = BGFX_TEXTURE_RT_WRITE_ONLY;
    if (size > bgfx::getCaps()->limits.maxTextureSize ||
        !bgfx::isTextureValid(0, false, 1, bgfx::TextureFormat::RGBA8, color_flags) ||
        !bgfx::isTextureValid(0, false, 1, bgfx::TextureFormat::D16, depth_flags))
    {
        return false;
    }
    color_ = bgfx::createTexture2D(size, size, false, 1, bgfx::TextureFormat::RGBA8, color_flags);
    depth_ = bgfx::createTexture2D(size, size, false, 1, bgfx::TextureFormat::D16, depth_flags);
    if (!bgfx::isValid(color_) || !bgfx::isValid(depth_))
    {
        release();
        return false;
    }
    const bgfx::TextureHandle attachments[] = {color_, depth_};
    framebuffer_ = bgfx::createFrameBuffer(2, attachments, false);
    vertex_ = bgfx::createShader(bgfx::copy(vs_shadow, sizeof(vs_shadow)));
    fragment_ = bgfx::createShader(bgfx::copy(fs_shadow, sizeof(fs_shadow)));
    if (bgfx::isValid(vertex_) && bgfx::isValid(fragment_))
    {
        program_ = bgfx::createProgram(vertex_, fragment_);
    }
    alpha_ = bgfx::createUniform("u_shadow_alpha", bgfx::UniformType::Vec4);
    alpha_sampler_ = bgfx::createUniform("s_shadow_alpha", bgfx::UniformType::Sampler);
    uv_transform_ = bgfx::createUniform("u_shadow_uv", bgfx::UniformType::Vec4);
    if (!bgfx::isValid(framebuffer_) || !bgfx::isValid(program_) || !bgfx::isValid(alpha_) ||
        !bgfx::isValid(alpha_sampler_) || !bgfx::isValid(uv_transform_))
    {
        release();
        return false;
    }
    resolution_ = size;
    return true;
}

std::uint32_t ShadowPass::resource_count() const noexcept
{
    return static_cast<std::uint32_t>(bgfx::isValid(color_)) +
           static_cast<std::uint32_t>(bgfx::isValid(depth_)) +
           static_cast<std::uint32_t>(bgfx::isValid(framebuffer_)) +
           static_cast<std::uint32_t>(bgfx::isValid(vertex_)) +
           static_cast<std::uint32_t>(bgfx::isValid(fragment_)) +
           static_cast<std::uint32_t>(bgfx::isValid(program_)) +
           static_cast<std::uint32_t>(bgfx::isValid(alpha_)) +
           static_cast<std::uint32_t>(bgfx::isValid(alpha_sampler_)) +
           static_cast<std::uint32_t>(bgfx::isValid(uv_transform_));
}

bool ShadowPass::bind_material(const Material& material, const std::vector<GpuTexture>& textures,
                               bgfx::TextureHandle fallback)
{
    const auto* shader = material.shader_data();
    if (shader == nullptr)
    {
        return false;
    }
    const auto parameter = [&shader](std::string_view name)
    {
        const auto found = std::find_if(shader->parameters.begin(), shader->parameters.end(),
                                        [name](const auto& item) { return item->name == name; });
        return found != shader->parameters.end() ? found->get() : nullptr;
    };
    float cutoff = 0.0F;
    if (const auto* definition = parameter("alphaCutoff"))
    {
        const auto* value = definition->default_value.AsFloatValue();
        if (value == nullptr)
        {
            return false;
        }
        cutoff = value->value;
        if ((definition->exposed && !material.get_float("alphaCutoff", cutoff)) ||
            !std::isfinite(cutoff) || cutoff < 0.0F || cutoff > 1.0F)
        {
            return false;
        }
    }
    float base_alpha = 1.0F;
    Vec4 uv_transform(1.0, 1.0, 0.0, 0.0);
    bgfx::TextureHandle texture = fallback;
    if (cutoff > 0.0F)
    {
        const auto* color = parameter("baseColor");
        const auto* value = color != nullptr ? color->default_value.AsVec4Value() : nullptr;
        if (value == nullptr)
        {
            return false;
        }
        Vec4 tint = value->value;
        if (color->exposed && !material.get_v4("baseColor", tint))
        {
            return false;
        }
        base_alpha = static_cast<float>(tint.w());
        const auto& resource = material.texture("baseColorTexture");
        const auto found =
            std::find_if(textures.begin(), textures.end(),
                         [&resource](const auto& item) { return item.resource == resource; });
        if (!resource || found == textures.end())
        {
            return false;
        }
        texture = found->handle;
        if (const auto* uv = parameter("uvTransform"))
        {
            const auto* transform = uv->default_value.AsVec4Value();
            if (transform == nullptr)
            {
                return false;
            }
            uv_transform = transform->value;
            if (uv->exposed && !material.get_v4("uvTransform", uv_transform))
            {
                return false;
            }
        }
    }
    const float alpha[] = {cutoff, base_alpha, 0.0F, 0.0F};
    bgfx::setUniform(alpha_, alpha);
    const float uv[] = {static_cast<float>(uv_transform.x()), static_cast<float>(uv_transform.y()),
                        static_cast<float>(uv_transform.z()), static_cast<float>(uv_transform.w())};
    bgfx::setUniform(uv_transform_, uv);
    bgfx::setTexture(0, alpha_sampler_, texture);
    return true;
}

bool ShadowPass::draw(const Scene& scene, const RenderSettings& settings, ShadowFrame& frame,
                      const std::vector<GpuMesh>& meshes, const std::vector<GpuTexture>& textures,
                      bgfx::TextureHandle fallback, bgfx::UniformHandle depth_uniform)
{
    struct Caster
    {
        const GpuMesh* mesh = nullptr;
        const MeshRenderer* renderer = nullptr;
        std::array<float, 16> model{};
        ShadowBounds bounds;
    };
    std::vector<Caster> casters;
    casters.reserve(meshes.size());
    for (const auto& mesh : meshes)
    {
        const Node* node = scene.find_node(mesh.node_id);
        const auto* renderer = node != nullptr ? node->get_component<MeshRenderer>() : nullptr;
        const auto* transform = node != nullptr ? node->get_component<Transform>() : nullptr;
        if (renderer != nullptr && !renderer->cast_shadows())
        {
            continue;
        }
        Caster caster;
        if (renderer == nullptr || renderer->mesh_data() == nullptr || transform == nullptr ||
            !transform->world_matrix(caster.model.data()))
        {
            return false;
        }
        caster.mesh = &mesh;
        caster.renderer = renderer;
        caster.bounds.reset();
        float combined[16]{};
        bx::mtxMul(combined, caster.model.data(), frame.view.data());
        for (std::uint16_t corner = 0; corner < 8; ++corner)
        {
            const std::array<float, 3> point = {
                (corner & 1) != 0 ? mesh.maximum[0] : mesh.minimum[0],
                (corner & 2) != 0 ? mesh.maximum[1] : mesh.minimum[1],
                (corner & 4) != 0 ? mesh.maximum[2] : mesh.minimum[2]};
            caster.bounds.include(transform_point(combined, point));
        }
        casters.push_back(caster);
    }
    const auto* caps = bgfx::getCaps();
    const float crop[] = {0.5F,
                          0.0F,
                          0.0F,
                          0.0F,
                          0.0F,
                          caps->originBottomLeft ? 0.5F : -0.5F,
                          0.0F,
                          0.0F,
                          0.0F,
                          0.0F,
                          caps->homogeneousDepth ? 0.5F : 1.0F,
                          0.0F,
                          0.5F,
                          0.5F,
                          caps->homogeneousDepth ? 0.5F : 0.0F,
                          1.0F};
    const float depth_scale[] = {caps->homogeneousDepth ? 0.5F : 1.0F,
                                 caps->homogeneousDepth ? 0.5F : 0.0F, 0.0F, 0.0F};
    for (std::uint16_t index = 0; index < settings.shadow_cascades; ++index)
    {
        auto bounds = frame.bounds[index];
        for (const auto& caster : casters)
        {
            if (bounds.overlaps_xy(caster.bounds))
            {
                // Include off-screen occluders whose light-space footprint reaches the
                // receiver slice. Camera-frustum culling alone would lose their shadows.
                bounds.minimum[2] = std::min(bounds.minimum[2], caster.bounds.minimum[2]);
                bounds.maximum[2] = std::max(bounds.maximum[2], caster.bounds.maximum[2]);
            }
        }
        const float margin = std::max(0.1F, (bounds.maximum[2] - bounds.minimum[2]) * 0.01F);
        bounds.minimum[2] -= margin;
        bounds.maximum[2] += margin;
        float projection[16]{};
        bx::mtxOrtho(projection, bounds.minimum[0], bounds.maximum[0], bounds.minimum[1],
                     bounds.maximum[1], -bounds.maximum[2], -bounds.minimum[2], 0.0F,
                     caps->homogeneousDepth, bx::Handedness::Right);
        float projected[16]{};
        bx::mtxMul(projected, projection, crop);
        bx::mtxMul(frame.matrices[index].data(), frame.view.data(), projected);
        if (!finite_matrix(frame.matrices[index].data()))
        {
            return false;
        }
        const auto view = static_cast<std::uint16_t>(shadow_view + index);
        const auto tile_x = static_cast<std::uint16_t>((index % 2) * frame.tile_size);
        const auto tile_y = static_cast<std::uint16_t>((index / 2) * frame.tile_size);
        bgfx::setViewFrameBuffer(view, framebuffer_);
        bgfx::setViewRect(view, tile_x, tile_y, frame.tile_size, frame.tile_size);
        bgfx::setViewClear(view, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0xffffffff, 1.0F, 0);
        bgfx::setViewTransform(view, frame.view.data(), projection);
        bgfx::touch(view);
        for (const auto& caster : casters)
        {
            if (!bounds.overlaps_xy(caster.bounds))
            {
                continue;
            }
            const auto& mesh = *caster.mesh;
            const auto* data = caster.renderer->mesh_data();
            const auto submit = [&](std::uint32_t first, std::uint32_t count, std::size_t slot)
            {
                const auto* material = caster.renderer->material(slot);
                if (material == nullptr || !bind_material(*material, textures, fallback))
                {
                    return false;
                }
                bgfx::setTransform(caster.model.data());
                bgfx::setUniform(depth_uniform, depth_scale);
                mesh.bind_vertices();
                bgfx::setIndexBuffer(mesh.indices, first, count);
                bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
                               BGFX_STATE_DEPTH_TEST_LESS |
                               cull_state(material->cull_mode(), caster.model.data()));
                bgfx::submit(view, program_);
                return true;
            };
            if (data->submeshes.empty() &&
                !submit(0, static_cast<std::uint32_t>(data->indices.size()), 0))
            {
                return false;
            }
            for (const auto& submesh : data->submeshes)
            {
                if (!submit(submesh.first_index(), submesh.index_count(), submesh.material_slot()))
                {
                    return false;
                }
            }
        }
    }
    return true;
}

} // namespace engine::render_detail
