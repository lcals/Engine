#include "runtime/render/passes/shadow_pass.hpp"

#include "runtime/performance/profiling.hpp"
#include "runtime/render/render_math.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/scene/material.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/node.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/schema/shader_generated.h"
#include "runtime/world/world_system.hpp"

#include "fs_shadow.bin.h"
#include "vs_shadow.bin.h"

#include <bx/math.h>

#include <SDL3/SDL_log.h>
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
        for (const auto view : attached_views_)
        {
            bgfx::setViewFrameBuffer(view, BGFX_INVALID_HANDLE);
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
    attached_views_.clear();
    last_skipped_lights_ = 0;
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
            if (!transform)
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

bool ShadowPass::draw(const WorldSystem& world, const RenderSettings& settings, ShadowFrame& frame,
                      const std::vector<GpuMesh>& meshes, const std::vector<GpuTexture>& textures,
                      bgfx::TextureHandle fallback, bgfx::UniformHandle depth_uniform,
                      const RenderConfig& config, std::span<const LocalShadowLight> local_lights,
                      std::span<ClusterLight> lights, const float* camera_world,
                      bgfx::ViewId base_view, const std::span<const std::uint64_t>* caster_ids)
{
    ENGINE_PROFILE_ZONE("Render.shadowSubmit");
    frame.local_records.clear();
    frame.skipped_local_lights = 0;
    for (auto& light : lights)
    {
        light.kind_inner[2] = light.kind_inner[3] = 0.0F;
    }
    const auto cascade_count = static_cast<std::uint16_t>(
        settings.directional_shadows && frame.tile_size != 0 ? settings.shadow_cascades : 0);
    const auto maximum_views =
        static_cast<std::uint32_t>(cascade_count) + config.maximum_local_shadow_faces;
    if (!bgfx::isValid(framebuffer_) || resolution_ != config.shadow_atlas_size ||
        (cascade_count != 0 && settings.shadow_resolution > resolution_) ||
        config.local_shadow_tile_size == 0 || config.local_shadow_tile_size > resolution_ ||
        camera_world == nullptr || !finite_matrix(camera_world) ||
        std::uint32_t{base_view} + maximum_views > bgfx::getCaps()->limits.maxViews)
    {
        return false;
    }
    struct Caster
    {
        const GpuMesh* mesh = nullptr;
        MeshRenderer renderer = {};
        std::array<float, 16> model{};
        std::array<std::array<float, 3>, 8> world_points{};
        ShadowBounds bounds;
    };
    std::vector<Caster> casters;
    casters.reserve(meshes.size());
    for (const auto& mesh : meshes)
    {
        if (caster_ids != nullptr &&
            std::find(caster_ids->begin(), caster_ids->end(), mesh.node_id) == caster_ids->end())
        {
            continue;
        }
        Node node = world.find_node(mesh.node_id);
        const auto renderer =
            static_cast<bool>(node) ? node.get_component<MeshRenderer>() : MeshRenderer{};
        const auto transform =
            static_cast<bool>(node) ? node.get_component<Transform>() : Transform{};
        if (static_cast<bool>(renderer) && !renderer.cast_shadows())
        {
            continue;
        }
        Caster caster;
        if (!renderer || renderer.mesh_data() == nullptr || !transform ||
            !transform.world_matrix(caster.model.data()))
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
            caster.world_points[corner] = transform_point(caster.model.data(), point);
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
    const auto submit_caster = [&](const Caster& caster, bgfx::ViewId view)
    {
        const auto& mesh = *caster.mesh;
        const auto* data = caster.renderer.mesh_data();
        const auto submit = [&](std::uint32_t first, std::uint32_t count, std::size_t slot)
        {
            const auto material = caster.renderer.material(slot);
            if (!material)
            {
                return false;
            }
            if (material.blend_mode() != schema::BlendMode::opaque)
            {
                return true;
            }
            if (!bind_material(material, textures, fallback))
            {
                return false;
            }
            bgfx::setTransform(caster.model.data());
            bgfx::setUniform(depth_uniform, depth_scale);
            mesh.bind_vertices();
            bgfx::setIndexBuffer(mesh.geometry->indices, first, count);
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
                           BGFX_STATE_DEPTH_TEST_LESS |
                           cull_state(material.cull_mode(), caster.model.data()));
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
        return true;
    };
    for (std::uint16_t index = 0; index < cascade_count; ++index)
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
        const auto view = static_cast<std::uint16_t>(base_view + index);
        const auto tile_x = static_cast<std::uint16_t>((index % 2) * frame.tile_size);
        const auto tile_y = static_cast<std::uint16_t>((index / 2) * frame.tile_size);
        const float scale = static_cast<float>(frame.tile_size) / resolution_;
        frame.rectangles[index] = {static_cast<float>(tile_x) / resolution_,
                                   caps->originBottomLeft
                                       ? 1.0F - static_cast<float>(tile_y) / resolution_ - scale
                                       : static_cast<float>(tile_y) / resolution_,
                                   scale, scale};
        bgfx::setViewFrameBuffer(view, framebuffer_);
        bgfx::setViewRect(view, tile_x, tile_y, frame.tile_size, frame.tile_size);
        bgfx::setViewClear(view, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0xffffffff, 1.0F, 0);
        bgfx::setViewTransform(view, frame.view.data(), projection);
        bgfx::touch(view);
        if (std::find(attached_views_.begin(), attached_views_.end(), view) ==
            attached_views_.end())
        {
            attached_views_.push_back(view);
        }
        for (const auto& caster : casters)
        {
            if (!bounds.overlaps_xy(caster.bounds))
            {
                continue;
            }
            if (!submit_caster(caster, view))
            {
                return false;
            }
        }
    }
    std::vector<const LocalShadowLight*> candidates;
    for (const auto& light : local_lights)
    {
        if (!light.settings.cast_shadows || light.settings.kind == schema::LightKind::directional)
        {
            continue;
        }
        if (light.light_index >= lights.size() || !finite_matrix(light.world.data()))
        {
            return false;
        }
        candidates.push_back(&light);
    }
    const auto distance_squared = [camera_world](const LocalShadowLight& light)
    {
        double distance = 0.0;
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            const double offset =
                static_cast<double>(light.world[12 + axis]) - camera_world[12 + axis];
            distance += offset * offset;
        }
        return distance;
    };
    std::sort(candidates.begin(), candidates.end(),
              [&](const auto* left, const auto* right)
              {
                  const auto a = distance_squared(*left);
                  const auto b = distance_squared(*right);
                  return a != b ? a < b : left->node_id < right->node_id;
              });
    // Fixed-size cells deliberately avoid a second atlas allocator. Smaller requested
    // faces occupy the upper-left of their cell; CSM keeps its original complete region.
    std::vector<std::array<std::uint16_t, 2>> free_tiles;
    const auto cell_size = config.local_shadow_tile_size;
    const auto cells_per_row = static_cast<std::uint16_t>(resolution_ / cell_size);
    for (std::uint16_t y = 0; y < cells_per_row; ++y)
    {
        for (std::uint16_t x = 0; x < cells_per_row; ++x)
        {
            const auto px = static_cast<std::uint16_t>(x * cell_size);
            const auto py = static_cast<std::uint16_t>(y * cell_size);
            if (cascade_count != 0 && px < settings.shadow_resolution &&
                py < settings.shadow_resolution)
            {
                continue;
            }
            free_tiles.push_back({px, py});
        }
    }
    const std::array<bx::Vec3, 6> point_directions = {
        bx::Vec3{1.0F, 0.0F, 0.0F},  bx::Vec3{-1.0F, 0.0F, 0.0F}, bx::Vec3{0.0F, 1.0F, 0.0F},
        bx::Vec3{0.0F, -1.0F, 0.0F}, bx::Vec3{0.0F, 0.0F, 1.0F},  bx::Vec3{0.0F, 0.0F, -1.0F}};
    const std::array<bx::Vec3, 6> point_ups = {
        bx::Vec3{0.0F, -1.0F, 0.0F}, bx::Vec3{0.0F, -1.0F, 0.0F}, bx::Vec3{0.0F, 0.0F, 1.0F},
        bx::Vec3{0.0F, 0.0F, -1.0F}, bx::Vec3{0.0F, -1.0F, 0.0F}, bx::Vec3{0.0F, -1.0F, 0.0F}};
    for (const auto* light : candidates)
    {
        const bool point = light->settings.kind == schema::LightKind::point;
        const std::uint16_t faces = point ? 6 : 1;
        const auto first_record = frame.local_records.size();
        if (first_record + faces > config.maximum_local_shadow_faces ||
            first_record + faces > free_tiles.size())
        {
            ++frame.skipped_local_lights;
            continue;
        }
        const auto size = std::min(light->settings.shadow_resolution, cell_size);
        const bx::Vec3 position{light->world[12], light->world[13], light->world[14]};
        const float near_plane = light->settings.range * 0.001F;
        const float far_plane = light->settings.range;
        if (!(near_plane > 0.0F && far_plane > near_plane))
        {
            return false;
        }
        for (std::uint16_t face = 0; face < faces; ++face)
        {
            bx::Vec3 direction = point_directions[face];
            bx::Vec3 up = point_ups[face];
            if (!point)
            {
                const auto& value = lights[light->light_index].direction_outer;
                direction = {value[0], value[1], value[2]};
                up = std::abs(direction.y) > 0.95F ? bx::Vec3{0.0F, 0.0F, 1.0F}
                                                   : bx::Vec3{0.0F, 1.0F, 0.0F};
            }
            float view_matrix[16]{};
            float projection[16]{};
            bx::mtxLookAt(view_matrix, position, bx::add(position, direction), up,
                          bx::Handedness::Right);
            bx::mtxProj(projection, point ? 90.0F : light->settings.outer_angle * 2.0F, 1.0F,
                        near_plane, far_plane, caps->homogeneousDepth, bx::Handedness::Right);
            ShadowRecord record;
            float projected[16]{};
            bx::mtxMul(projected, projection, crop);
            bx::mtxMul(record.matrix.data(), view_matrix, projected);
            if (!finite_matrix(record.matrix.data()))
            {
                return false;
            }
            const auto tile = free_tiles[first_record + face];
            const float scale = static_cast<float>(size) / resolution_;
            record.rectangle = {static_cast<float>(tile[0]) / resolution_,
                                caps->originBottomLeft
                                    ? 1.0F - static_cast<float>(tile[1]) / resolution_ - scale
                                    : static_cast<float>(tile[1]) / resolution_,
                                scale, scale};
            record.parameters = {light->settings.shadow_bias, 1.0F / resolution_, near_plane,
                                 far_plane};
            const auto view =
                static_cast<bgfx::ViewId>(base_view + cascade_count + first_record + face);
            bgfx::setViewName(view, point ? "Point shadow face" : "Spot shadow");
            bgfx::setViewFrameBuffer(view, framebuffer_);
            bgfx::setViewRect(view, tile[0], tile[1], size, size);
            bgfx::setViewClear(view, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0xffffffff, 1.0F, 0);
            bgfx::setViewTransform(view, view_matrix, projection);
            bgfx::touch(view);
            if (std::find(attached_views_.begin(), attached_views_.end(), view) ==
                attached_views_.end())
            {
                attached_views_.push_back(view);
            }
            float view_projection[16]{};
            bx::mtxMul(view_projection, view_matrix, projection);
            for (const auto& caster : casters)
            {
                // Reject only when all eight world-AABB corners are beyond one clip
                // plane. This also includes off-camera shadow casters.
                std::uint32_t outside = 63;
                for (const auto& corner : caster.world_points)
                {
                    float clip[4]{};
                    for (std::size_t row = 0; row < 4; ++row)
                    {
                        clip[row] = view_projection[row] * corner[0] +
                                    view_projection[4 + row] * corner[1] +
                                    view_projection[8 + row] * corner[2] +
                                    view_projection[12 + row];
                    }
                    std::uint32_t mask = 0;
                    if (clip[0] < -clip[3])
                    {
                        mask |= 1;
                    }
                    if (clip[0] > clip[3])
                    {
                        mask |= 2;
                    }
                    if (clip[1] < -clip[3])
                    {
                        mask |= 4;
                    }
                    if (clip[1] > clip[3])
                    {
                        mask |= 8;
                    }
                    if (clip[2] < (caps->homogeneousDepth ? -clip[3] : 0.0F))
                    {
                        mask |= 16;
                    }
                    if (clip[2] > clip[3])
                    {
                        mask |= 32;
                    }
                    outside &= mask;
                }
                if (outside == 0 && !submit_caster(caster, view))
                {
                    return false;
                }
            }
            frame.local_records.push_back(record);
        }
        lights[light->light_index].kind_inner[2] = static_cast<float>(first_record + 1);
        lights[light->light_index].kind_inner[3] = static_cast<float>(faces);
    }
    if (frame.skipped_local_lights != last_skipped_lights_)
    {
        if (frame.skipped_local_lights != 0)
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
                        "Shadow atlas budget: %u local lights have no shadow allocation",
                        frame.skipped_local_lights);
        }
        last_skipped_lights_ = frame.skipped_local_lights;
    }
    return true;
}

} // namespace engine::render_detail
