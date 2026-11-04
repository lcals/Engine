#include "runtime/render/render_system.hpp"
#include "runtime/scene/ecs_scene.hpp"
#include "runtime/scene/ecs_storage.hpp"

#include "runtime/font/font_system.hpp"
#include "runtime/performance/profiling.hpp"
#include "runtime/render/render_constants.hpp"
#include "runtime/render/scene_gpu_resources.hpp"
#include "runtime/render/scene_render_query.hpp"
#include "runtime/resource/resource.hpp"
#include "runtime/scene/light.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/task/task_system.hpp"
#include "runtime/ui/ui_system.hpp"
#include "runtime/world/world_system.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <iterator>
#include <new>
#include <set>
#include <string>

namespace engine
{

bool RenderSystem::prepare_scene(Camera camera)
{
    ENGINE_PROFILE_ZONE("Render.prepareSceneResources");
    if (!is_initialized() || (camera.instance_id() != 0 && !camera))
    {
        return false;
    }
    RenderSettings current_settings;
    if (!resolve_settings(current_settings))
    {
        return false;
    }
    const Scene* scene = world_ != nullptr ? world_->scene() : nullptr;
    if (ui_ && !ui_->synchronize())
    {
        return false;
    }
    if (world_ == nullptr || world_->scenes().empty())
    {
        release_scene();
        return true;
    }
    const bool capture_active = capture_status_ == SceneCaptureStatus::pending ||
                                capture_status_ == SceneCaptureStatus::complete ||
                                hdr_capture_status_ == SceneCaptureStatus::pending ||
                                hdr_capture_status_ == SceneCaptureStatus::complete;
    if (scene_resources_ == nullptr && !capture_active &&
        !render_detail::drawable_scene(*world_, camera, !extensions_.empty()))
    {
        return true;
    }
    std::size_t lights = 0;
    auto light_query = flecs::world(world_->native_world())
                           .query_builder<const ecs_detail::ComponentStorage<Light::Data>,
                                          const ecs_detail::NodeRuntime>()
                           .with<ecs_detail::Published>()
                           .build();
    light_query.each(
        [&](const auto& stored, const ecs_detail::NodeRuntime& state)
        {
            if (state.active && state.visible_in_hierarchy && stored.state.flags.enabled &&
                stored.state.flags.visible)
            {
                ++lights;
            }
        });
    if (lights > config_.maximum_lights)
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Scene exceeds the limit of %u active lights",
                     config_.maximum_lights);
        release_scene();
        return false;
    }
    if (!renderer_initialized_)
    {
        // Android 暂停时保留 CPU 场景，恢复 Surface 后再创建 GPU 对象。
        return true;
    }
    if (scene_resources_ == nullptr)
    {
        scene_resources_.reset(new (std::nothrow) SceneGpuResources{});
        if (scene_resources_ == nullptr)
        {
            return false;
        }
        if (!scene_resources_->initialize())
        {
            release_scene();
            return false;
        }
    }
    auto& resources = *scene_resources_;
    if (!resources.prepare_ibl(scene, config_.texture_staging_bytes))
    {
        release_scene();
        return false;
    }
    auto& programs = resources.programs;
    auto& meshes = resources.meshes;
    auto& textures = resources.textures;
    std::set<std::string> needed_programs;
    std::set<std::uint64_t> needed_meshes;
    std::set<std::uint64_t> needed_mesh_instances;
    std::set<const Resource*> needed_textures;
    std::vector<render_detail::PreparedTexture> missing_textures;
    bool needs_skinning = false;
    {
        const auto process =
            [&](Node node, const ecs_detail::ComponentStorage<MeshRenderer::Data>& stored) -> bool
        {
            const auto* owner = node.scene();
            (void)owner;
            (void)stored;

            const auto renderer = node.get_component<MeshRenderer>();
            if (!renderer || !renderer.active() || !renderer.visible_in_hierarchy())
            {
                return true;
            }
            const auto* data = renderer.mesh_data();
            if (data == nullptr)
            {
                release_scene();
                return false;
            }
            needed_meshes.insert(node.id());
            needed_mesh_instances.insert(renderer.instance_id());
            if (!renderer.skinning_palette().empty())
            {
                needs_skinning = true;
                if (!resources.skinning.prepare())
                {
                    release_scene();
                    return false;
                }
            }
            auto mesh = std::find_if(meshes.begin(), meshes.end(), [&node](const auto& item)
                                     { return item.node_id == node.id(); });
            if (mesh == meshes.end())
            {
                meshes.emplace_back();
                mesh = std::prev(meshes.end());
            }
            if (mesh->instance_id != renderer.instance_id() ||
                mesh->revision != renderer.mesh_revision())
            {
                ENGINE_PROFILE_ZONE("Render.rebuildMeshResource");
                const auto source = renderer.mesh_resource();
                std::shared_ptr<render_detail::GpuGeometry> geometry;
                if (source)
                {
                    const auto cached = resources.geometries.find(source.get());
                    if (cached != resources.geometries.end())
                    {
                        geometry = cached->second.lock();
                    }
                }
                if (!geometry)
                {
                    geometry = std::make_shared<render_detail::GpuGeometry>();
                    if (!geometry->create(renderer))
                    {
                        release_scene();
                        return false;
                    }
                    if (source)
                    {
                        resources.geometries[source.get()] = geometry;
                    }
                }
                if (!mesh->create(renderer, std::move(geometry)))
                {
                    release_scene();
                    return false;
                }
            }
            if (!mesh->update_deformation(renderer, resources.skinning))
            {
                release_scene();
                return false;
            }
            const auto lightmap = renderer.lightmap_resource();
            if (lightmap && needed_textures.insert(lightmap.get()).second &&
                std::none_of(textures.begin(), textures.end(),
                             [&lightmap](const auto& item) { return item.resource == lightmap; }))
            {
                missing_textures.emplace_back();
                if (!missing_textures.back().describe(lightmap, *bgfx::getCaps(),
                                                      config_.texture_staging_bytes))
                {
                    release_scene();
                    return false;
                }
            }
            std::set<std::size_t> slots;
            if (data->submeshes.empty())
            {
                slots.insert(0);
            }
            for (const auto& submesh : data->submeshes)
            {
                slots.insert(submesh.material_slot());
            }
            for (auto slot : slots)
            {
                const auto material = renderer.material(slot);
                if (!material)
                {
                    release_scene();
                    return false;
                }
                const std::string path(material.shader_path());
                const auto* shader = material.shader_data();
                if (shader == nullptr)
                {
                    SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Unknown shader resource: %s",
                                 path.c_str());
                    release_scene();
                    return false;
                }
                if (needed_programs.insert(path).second)
                {
                    auto program =
                        std::find_if(programs.begin(), programs.end(),
                                     [&path](const auto& item) { return item.path == path; });
                    if (program == programs.end())
                    {
                        programs.emplace_back();
                        program = std::prev(programs.end());
                    }
                    if (program->definition != *shader || !bgfx::isValid(program->program))
                    {
                        program->release();
                        if (!program->create(*shader))
                        {
                            SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                                         "Cannot prepare shader resource: %s", path.c_str());
                            release_scene();
                            return false;
                        }
                    }
                }
                for (const auto& sampler : shader->samplers)
                {
                    if (sampler->source != schema::ShaderTextureSource::resource)
                    {
                        continue;
                    }
                    const auto& source = material.texture(sampler->name);
                    if (!source || source->texture() == nullptr)
                    {
                        release_scene();
                        return false;
                    }
                    if (needed_textures.insert(source.get()).second &&
                        std::none_of(textures.begin(), textures.end(), [&source](const auto& item)
                                     { return item.resource == source; }))
                    {
                        missing_textures.emplace_back();
                        if (!missing_textures.back().describe(source, *bgfx::getCaps(),
                                                              config_.texture_staging_bytes))
                        {
                            SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                                         "Unsupported texture or staging budget exceeded: %s",
                                         source->info().path.c_str());
                            release_scene();
                            return false;
                        }
                    }
                }
            }

            return true;
        };
        auto query = flecs::world(world_->native_world())
                         .query_builder<const ecs_detail::ComponentStorage<MeshRenderer::Data>>()
                         .with<ecs_detail::Published>()
                         .build();
        bool query_success = true;
        query.each(
            [&](flecs::entity entity,
                const ecs_detail::ComponentStorage<MeshRenderer::Data>& stored)
            {
                if (query_success)
                {
                    query_success = process(world_->find_node(entity.id()), stored);
                }
            });
        if (!query_success)
        {
            return false;
        }
    }
    for (std::size_t begin = 0; begin < missing_textures.size();)
    {
        std::size_t end = begin;
        std::size_t bytes = 0;
        while (end < missing_textures.size() && end - begin < config_.texture_batch_size &&
               missing_textures[end].bytes <= config_.texture_staging_bytes - bytes)
        {
            bytes += missing_textures[end].bytes;
            ++end;
        }
        // describe 已拒绝超过单项预算的纹理，每批至少包含一项。
        std::future<bool> prepared;
        if (tasks_ != nullptr)
        {
            std::vector<TaskDescription> batch;
            for (std::size_t index = begin; index < end; ++index)
            {
                auto* texture = &missing_textures[index];
                TaskDescription task;
                task.name = "Render.texturePrepare";
                task.execute = [texture] { return texture->prepare(); };
                batch.push_back(std::move(task));
            }
            prepared = tasks_->submit_batch(std::move(batch));
        }
        {
            ENGINE_PROFILE_ZONE("Render.textureJoin");
            const bool ready =
                prepared.valid()
                    ? prepared.get()
                    : std::all_of(missing_textures.begin() + begin, missing_textures.begin() + end,
                                  [](auto& texture) { return texture.prepare(); });
            if (!ready)
            {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Cannot prepare texture batch");
                release_scene();
                return false;
            }
        }
        ENGINE_PROFILE_PLOT("Render.textureStagingBytes", static_cast<double>(bytes));
        for (std::size_t index = begin; index < end; ++index)
        {
            auto& texture = missing_textures[index];
            textures.emplace_back();
            if (!textures.back().create(texture))
            {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Cannot upload texture resource: %s",
                             texture.resource->info().path.c_str());
                release_scene();
                return false;
            }
            texture.levels.clear();
        }
        begin = end;
    }
    for (auto item = meshes.begin(); item != meshes.end();)
    {
        if (!needed_meshes.contains(item->node_id))
        {
            item->release();
            item = meshes.erase(item);
        }
        else
        {
            ++item;
        }
    }
    for (auto item = programs.begin(); item != programs.end();)
    {
        if (!needed_programs.contains(item->path))
        {
            item->release();
            item = programs.erase(item);
        }
        else
        {
            ++item;
        }
    }
    std::erase_if(resources.geometries, [](const auto& item) { return item.second.expired(); });
    const auto retired_lod = [&needed_mesh_instances](const auto& entry)
    { return !needed_mesh_instances.contains(entry.first.first); };
    std::erase_if(resources.selected_lods, retired_lod);
    std::erase_if(resources.selected_lod_revisions, retired_lod);
    if (!needs_skinning)
    {
        resources.skinning.release();
    }
    for (auto item = textures.begin(); item != textures.end();)
    {
        if (!needed_textures.contains(item->resource.get()))
        {
            item->release();
            item = textures.erase(item);
        }
        else
        {
            ++item;
        }
    }
    if (!capture_active && !render_detail::drawable_scene(*world_, camera, !extensions_.empty()))
    {
        resources.shadow.release();
        resources.ambient_occlusion.release();
        resources.hdr_post_process.release();
        if (meshes.empty())
        {
            release_draw_resources();
        }
    }
    return true;
}

void RenderSystem::release_scene() noexcept
{
    clear_debug_lines();
    feature_applied_.fill(false);
    cancel_hdr_capture();
    if (capture_status_ == SceneCaptureStatus::pending ||
        capture_status_ == SceneCaptureStatus::complete)
    {
        capture_status_ = SceneCaptureStatus::cancelled;
    }
    capture_request_ = {};
    capture_snapshot_.clear();
    release_extensions_scene();
    if (ui_)
    {
        ui_->release_scene();
    }
    if (fonts_)
    {
        fonts_->release_scene();
    }
    release_draw_resources();
}

void RenderSystem::release_draw_resources() noexcept
{
    if (scene_resources_)
    {
        cancel_hdr_capture();
    }
    // Complete GPU readback while its destination and the renderer still exist.
    // Keep the public frame counter in sync with these teardown submissions.
    while (scene_resources_ &&
           (scene_resources_->capture_pending || scene_resources_->hdr_capture_pending))
    {
        frame_number_ = bgfx::frame();
        if (static_cast<std::int32_t>(frame_number_ - scene_resources_->capture_ready_frame) >= 0)
        {
            scene_resources_->capture_pending = false;
        }
        if (scene_resources_->hdr_capture_pending &&
            static_cast<std::int32_t>(frame_number_ - scene_resources_->hdr_capture_ready_frame) >=
                0)
        {
            scene_resources_->hdr_capture_pending = false;
        }
    }
    scene_resources_.reset();
}

bool RenderSystem::scene_prepared() const noexcept
{
    return scene_resources_ != nullptr;
}

std::uint32_t RenderSystem::gpu_resource_count() const noexcept
{
    return scene_resources_ != nullptr ? scene_resources_->resource_count() : 0;
}

} // namespace engine
