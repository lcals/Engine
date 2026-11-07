#include "runtime/font/font_system.hpp"
#include "runtime/performance/profiling.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/render/scene_gpu_resources.hpp"
#include "runtime/render/scene_render_query.hpp"
#include "runtime/resource/resource.hpp"
#include "runtime/scene/ecs_scene.hpp"
#include "runtime/scene/ecs_storage.hpp"
#include "runtime/scene/environment_component.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/task/task_system.hpp"
#include "runtime/ui/ui_system.hpp"
#include "runtime/world/world_system.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <string>

namespace engine
{
using PreparationClock = std::chrono::steady_clock;

bool RenderSystem::RenderPreparation::request_texture(ResourceLoader* loader, ResourceHandle handle)
{
    const TextureResource* source = nullptr;
    if (!loader || loader->get(handle, source) != ResourceError::none)
    {
        return false;
    }
    if (ready_textures.contains(source) || textures.contains(source))
    {
        return true;
    }
    auto work = std::make_shared<render_detail::TexturePrepareWork>();
    if (!work->source.retain(loader, handle))
    {
        return false;
    }
    work->generation = generation;
    textures.emplace(source, std::move(work));
    return true;
}

void RenderSystem::RenderPreparation::detach_observers() noexcept
{
    if (observed_world && observed_world->is_initialized() &&
        observed_world->epoch() == world_epoch)
    {
        flecs::world current(observed_world->native_world());
        for (auto id : observers)
        {
            current.entity(id).destruct();
        }
    }
    observers.clear();
    observed_world = nullptr;
}

RenderPrepareStatus RenderSystem::preparation_status(RenderPrepareProgress& output) const noexcept
{
    output = preparation_ ? preparation_->progress : RenderPrepareProgress{};
    return preparation_ ? preparation_->status : RenderPrepareStatus::idle;
}

std::string_view RenderSystem::preparation_error() const noexcept
{
    return preparation_ ? std::string_view(preparation_->error) : std::string_view{};
}

RenderPrepareStatus RenderSystem::prepare_scene(RenderPrepareMode mode,
                                                RenderPrepareProgress& output, double max_seconds,
                                                Camera camera)
{
    ENGINE_PROFILE_ZONE("Render.prepareSceneResources");
    output = {};
    if (!preparation_)
    {
        preparation_ = std::make_unique<RenderPreparation>();
    }
    auto& state = *preparation_;
    const auto fail = [&](std::string_view error)
    {
        state.error = error;
        for (auto& [source, work] : state.textures)
        {
            state.retired_work.push_back(std::move(work));
        }
        state.textures.clear();
        release_draw_resources();
        state.status = RenderPrepareStatus::failed;
        return state.status;
    };
    if (!is_initialized() || (camera.instance_id() != 0 && !camera) ||
        !std::isfinite(max_seconds) || max_seconds < 0 ||
        (mode == RenderPrepareMode::synchronous && max_seconds != 0) ||
        (mode != RenderPrepareMode::synchronous && mode != RenderPrepareMode::incremental))
    {
        return fail("Invalid render preparation request");
    }
    const bool synchronous = mode == RenderPrepareMode::synchronous;
    const auto begin = PreparationClock::now();
    const auto seconds = max_seconds == 0 ? config_.prepare_seconds_per_step
                                          : std::min(max_seconds, config_.prepare_seconds_per_step);
    std::uint32_t items = 0;
    std::uint64_t uploaded = 0;
    const auto budget = [&]
    {
        return synchronous ||
               (items < config_.prepare_items_per_step &&
                std::chrono::duration<double>(PreparationClock::now() - begin).count() < seconds);
    };
    if (world_ && !world_->scenes().empty())
    {
        if (!prepare_frame())
        {
            if (synchronous)
            {
                return fail("Rendering surface is unavailable");
            }
            output = state.progress;
            if (state.status != RenderPrepareStatus::failed)
            {
                state.status = RenderPrepareStatus::preparing;
            }
            return state.status;
        }
    }
    collect_preparation(false);
    std::vector<std::uint64_t> scenes;
    if (world_)
    {
        for (const auto& scene : world_->scenes())
        {
            scenes.push_back(scene->id());
        }
    }
    const auto epoch = world_ ? world_->epoch() : 0;
    if (state.observed_world != world_ || state.world_epoch != epoch ||
        (!state.scenes.empty() && scenes.empty()))
    {
        if (state.observed_world || scene_resources_)
        {
            release_scene();
        }
        state.scenes = std::move(scenes);
        state.world_epoch = epoch;
        state.observed_world = world_;
        state.discover = true;
        state.ibl_dirty = true;
    }
    else if (state.scenes != scenes && state.status != RenderPrepareStatus::failed)
    {
        // 场景集合改变只撤销未完成工作；常驻实例与已就绪的共享资源仍然有效。
        for (auto& [source, work] : state.textures)
        {
            if (scene_resources_)
            {
                scene_resources_->textures.erase(source);
            }
            state.retired_work.push_back(std::move(work));
        }
        state.textures.clear();
        state.scenes = std::move(scenes);
        state.progress.generation = ++state.generation;
        state.progress.discovery_complete = false;
        state.status = RenderPrepareStatus::preparing;
        state.discover = true;
        state.ibl_dirty = true;
    }
    if (state.status == RenderPrepareStatus::failed)
    {
        output = state.progress;
        return state.status;
    }
    if (state.status == RenderPrepareStatus::idle)
    {
        ++state.generation;
        state.status = RenderPrepareStatus::preparing;
        state.progress.generation = state.generation;
    }
    if (!world_ || state.scenes.empty())
    {
        state.progress.discovery_complete = true;
        state.status = RenderPrepareStatus::ready;
        output = state.progress;
        return state.status;
    }
    if (!world_->refresh_scene_state() || (ui_ && !ui_->synchronize()))
    {
        return fail("Cannot synchronize scene state");
    }
    const auto selected_camera = camera            ? camera
                                 : world_->scene() ? world_->scene()->active_camera()
                                                   : Camera{};
    if (state.camera_instance != selected_camera.instance_id())
    {
        state.camera_instance = selected_camera.instance_id();
        state.ibl_dirty = true;
        state.status = RenderPrepareStatus::preparing;
    }
    if (!scene_resources_ && state.status == RenderPrepareStatus::ready &&
        state.dirty_nodes.empty() && !state.ibl_dirty && !state.discover &&
        !render_detail::drawable_scene(*world_, camera, !extensions_.empty()))
    {
        output = state.progress;
        return state.status;
    }
    if (!scene_resources_)
    {
        scene_resources_ = std::make_unique<SceneGpuResources>();
        if (!scene_resources_->initialize())
        {
            return fail("Cannot initialize scene GPU resources");
        }
    }
    auto& resources = *scene_resources_;
    if (state.observers.empty())
    {
        flecs::world current(world_->native_world());
        auto* pending = &state;
        const auto dirty = [pending](std::uint64_t id)
        {
            pending->dirty_nodes.insert(id);
            pending->progress.discovery_complete = false;
            if (pending->status == RenderPrepareStatus::ready)
            {
                pending->status = RenderPrepareStatus::preparing;
            }
        };
        state.observers.push_back(
            current.observer()
                .with<ecs_detail::Published>()
                .event<Component>()
                .run(
                    [pending, dirty](flecs::iter& it)
                    {
                        while (it.next())
                        {
                            const auto* value = it.param<Component>();
                            if (value && value->type() == ComponentType::MeshRendererData)
                            {
                                dirty(value->node().id());
                            }
                            if (value && value->type() == ComponentType::EnvironmentData)
                            {
                                pending->ibl_dirty = true;
                                dirty(value->node().id());
                            }
                        }
                    })
                .id());
        state.observers.push_back(current.observer()
                                      .with<ecs_detail::Published>()
                                      .event<Node>()
                                      .run(
                                          [pending, dirty](flecs::iter& it)
                                          {
                                              while (it.next())
                                              {
                                                  pending->discover = true;
                                                  pending->ibl_dirty = true;
                                                  for (auto row : it)
                                                  {
                                                      dirty(it.entity(row).id());
                                                  }
                                              }
                                          })
                                      .id());
        state.observers.push_back(current.observer()
                                      .with<ecs_detail::Published>()
                                      .event(flecs::OnAdd)
                                      .event(flecs::OnRemove)
                                      .run(
                                          [pending, dirty](flecs::iter& it)
                                          {
                                              while (it.next())
                                              {
                                                  pending->ibl_dirty = true;
                                                  for (auto row : it)
                                                  {
                                                      dirty(it.entity(row).id());
                                                  }
                                              }
                                          })
                                      .id());
        state.observers.push_back(current.observer()
                                      .with<ecs_detail::ComponentStorage<MeshRenderer::Data>>()
                                      .with<ecs_detail::Published>()
                                      .event(flecs::OnAdd)
                                      .event(flecs::OnRemove)
                                      .run(
                                          [dirty](flecs::iter& it)
                                          {
                                              while (it.next())
                                              {
                                                  for (auto row : it)
                                                  {
                                                      dirty(it.entity(row).id());
                                                  }
                                              }
                                          })
                                      .id());
        state.observers.push_back(
            current.observer()
                .with<ecs_detail::ComponentStorage<EnvironmentComponent::Data>>()
                .with<ecs_detail::Published>()
                .event(flecs::OnAdd)
                .event(flecs::OnRemove)
                .run(
                    [pending, dirty](flecs::iter& it)
                    {
                        while (it.next())
                        {
                            pending->ibl_dirty = true;
                            for (auto row : it)
                            {
                                dirty(it.entity(row).id());
                            }
                        }
                    })
                .id());
    }
    if (state.discover)
    {
        // 首次登记或层级可见性改变时建立实体集合；日常资源改变由通知定位。
        for (const auto& mesh : resources.meshes)
        {
            state.dirty_nodes.insert(mesh.node_id);
        }
        auto query = flecs::world(world_->native_world())
                         .query_builder<const ecs_detail::ComponentStorage<MeshRenderer::Data>>()
                         .with<ecs_detail::Published>()
                         .build();
        query.each([&](flecs::entity entity, const auto&)
                   { state.dirty_nodes.insert(entity.id()); });
        state.discover = false;
    }
    auto* loader = world_->resource_loader();
    while (!state.dirty_nodes.empty() && budget())
    {
        ++items;
        const auto id = *state.dirty_nodes.begin();
        state.dirty_nodes.erase(state.dirty_nodes.begin());
        const auto node = world_->find_node(id);
        const auto renderer = node.get_component<MeshRenderer>();
        auto found = resources.mesh_index.lower_bound({epoch, id, 0});
        const bool exists = found != resources.mesh_index.end() &&
                            found->first.world_epoch == epoch && found->first.entity == id;
        auto* mesh = exists ? &resources.meshes[found->second] : nullptr;
        std::vector<const Resource*> previous_textures;
        std::vector<const schema::ShaderDataT*> previous_programs;
        const Resource* previous_geometry = nullptr;
        if (mesh)
        {
            previous_textures = mesh->textures;
            previous_programs = mesh->programs;
            previous_geometry = mesh->geometry ? mesh->geometry->resource.get() : nullptr;
        }
        const bool visible = renderer && renderer.active() && renderer.visible_in_hierarchy();
        if (!visible)
        {
            if (exists)
            {
                const auto index = found->second;
                resources.mesh_index.erase(found);
                const auto instance = resources.meshes[index].instance_id;
                const auto old_lod = [instance](const auto& entry)
                { return entry.first.first == instance; };
                std::erase_if(resources.selected_lods, old_lod);
                std::erase_if(resources.selected_lod_revisions, old_lod);
                if (index + 1 != resources.meshes.size())
                {
                    resources.meshes[index] = std::move(resources.meshes.back());
                    const auto& moved = resources.meshes[index];
                    resources.mesh_index[{epoch, moved.node_id, moved.instance_id}] = index;
                }
                resources.meshes.pop_back();
            }
            mesh = nullptr;
        }
        else
        {
            const Resource* source = nullptr;
            const auto* data = renderer.mesh_data();
            if (!data || !loader ||
                loader->get(renderer.mesh_resource(), source) != ResourceError::none)
            {
                return fail("Mesh binding is not loaded");
            }
            if (!exists)
            {
                resources.meshes.emplace_back();
                mesh = &resources.meshes.back();
            }
            else if (mesh->instance_id != renderer.instance_id())
            {
                resources.mesh_index.erase(found);
            }
            if (mesh->instance_id != renderer.instance_id() ||
                mesh->revision != renderer.mesh_revision())
            {
                auto geometry = resources.geometries[source].lock();
                if (!geometry)
                {
                    geometry = std::make_shared<render_detail::GpuGeometry>();
                    if (!geometry->create(renderer))
                    {
                        return fail("Cannot create GPU geometry");
                    }
                    resources.geometries[source] = geometry;
                }
                if (!mesh->create(renderer, std::move(geometry)))
                {
                    return fail("Cannot create GPU mesh instance");
                }
            }
            resources.mesh_index[{epoch, id, renderer.instance_id()}] =
                mesh - resources.meshes.data();
            mesh->textures.clear();
            mesh->programs.clear();
            const auto texture = [&](ResourceHandle handle)
            {
                const Resource* source = nullptr;
                if (!loader || loader->get(handle, source) != ResourceError::none ||
                    !state.request_texture(loader, handle))
                {
                    return false;
                }
                if (std::find(mesh->textures.begin(), mesh->textures.end(), source) ==
                    mesh->textures.end())
                {
                    mesh->textures.push_back(source);
                }
                return true;
            };
            if (renderer.lightmap_resource().id && !texture(renderer.lightmap_resource()))
            {
                return fail("Lightmap binding is not loaded");
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
                const auto* shader = material.shader_data();
                if (!material || !shader)
                {
                    return fail("Material or shader binding is not loaded");
                }
                auto [program, inserted] = resources.programs.try_emplace(shader);
                if (inserted && !program->second.resource.retain(loader, material.shader()))
                {
                    return fail("Shader binding is not loaded");
                }
                if (inserted)
                {
                    state.pending_programs.insert(shader);
                }
                if (std::find(mesh->programs.begin(), mesh->programs.end(), shader) ==
                    mesh->programs.end())
                {
                    mesh->programs.push_back(shader);
                }
                for (const auto& sampler : shader->samplers)
                {
                    if (sampler->source == schema::ShaderTextureSource::resource &&
                        !texture(material.texture(sampler->name)))
                    {
                        return fail("Material texture binding is not loaded");
                    }
                }
            }
        }
        const auto update_users =
            [](auto& users, auto& unused, const auto& previous, const auto& next)
        {
            for (auto key : previous)
            {
                if (std::find(next.begin(), next.end(), key) == next.end() && --users[key] == 0)
                {
                    unused.insert(key);
                }
            }
            for (auto key : next)
            {
                if (std::find(previous.begin(), previous.end(), key) == previous.end())
                {
                    ++users[key];
                    unused.erase(key);
                }
            }
        };
        update_users(state.texture_users, state.unused_textures, previous_textures,
                     mesh ? mesh->textures : std::vector<const Resource*>{});
        update_users(state.program_users, state.unused_programs, previous_programs,
                     mesh ? mesh->programs : std::vector<const schema::ShaderDataT*>{});
        if (previous_geometry)
        {
            const auto old = resources.geometries.find(previous_geometry);
            if (old != resources.geometries.end() && old->second.expired())
            {
                resources.geometries.erase(old);
            }
        }
    }
    if (state.ibl_dirty && budget())
    {
        ++items;
        if (!resources.collect_ibl(world_->scene(), state))
        {
            return fail("Invalid IBL resource binding");
        }
        state.ibl_dirty = false;
    }
    while (!state.unused_textures.empty() && budget())
    {
        ++items;
        const auto source = *state.unused_textures.begin();
        const auto work = state.textures.find(source);
        if (work != state.textures.end())
        {
            state.retired_work.push_back(std::move(work->second));
            state.textures.erase(work);
        }
        state.ready_textures.erase(source);
        resources.textures.erase(source);
        state.texture_users.erase(source);
        state.unused_textures.erase(state.unused_textures.begin());
    }
    while (!state.unused_programs.empty() && budget())
    {
        ++items;
        const auto source = *state.unused_programs.begin();
        state.pending_programs.erase(source);
        resources.programs.erase(source);
        state.program_users.erase(source);
        state.unused_programs.erase(state.unused_programs.begin());
    }
    while (!state.pending_programs.empty() && budget())
    {
        ++items;
        const auto source = *state.pending_programs.begin();
        auto& program = resources.programs.find(source)->second;
        auto retained = std::move(program.resource);
        if (!program.create(*source))
        {
            return fail("Cannot create GPU program");
        }
        program.resource = std::move(retained);
        state.pending_programs.erase(state.pending_programs.begin());
    }
    // 每个工作项拥有稳定源获取。预算计入正在执行及已完成等待上传的缓冲。
    do
    {
        for (auto item = state.textures.begin(); item != state.textures.end() && budget();)
        {
            auto& work = *item->second;
            if (!synchronous && uploaded >= config_.upload_bytes_per_step)
            {
                break;
            }
            if (!work.reserved_bytes)
            {
                if (state.active_jobs >= config_.texture_batch_size)
                {
                    ++item;
                    continue;
                }
                work.part.mip = work.mip;
                work.part.face = work.face;
                work.part.byte_offset = work.byte_offset;
                const bool native = work.source.data<TextureResource>()->encoding ==
                                    schema::TextureEncoding::native;
                const auto maximum = native ? std::min<std::uint64_t>(config_.texture_staging_bytes,
                                                                      config_.upload_bytes_per_step)
                                            : config_.texture_staging_bytes;
                if (!work.part.describe(loader, work.source.handle(), *bgfx::getCaps(), maximum))
                {
                    return fail(
                        "Unsupported texture format or indivisible mip exceeds staging budget");
                }
                if (work.part.bytes > config_.texture_staging_bytes - state.staging_bytes)
                {
                    ++item;
                    continue;
                }
                if (!tasks_ && !synchronous && !native)
                {
                    return fail("Incremental Basis preparation requires a render task domain");
                }
                work.reserved_bytes = work.part.bytes;
                state.staging_bytes += work.reserved_bytes;
                ++state.active_jobs;
                ++items;
                if (tasks_)
                {
                    std::weak_ptr<render_detail::TexturePrepareWork> weak = item->second;
                    work.completion = tasks_->submit("Render.texturePrepare",
                                                     [weak]
                                                     {
                                                         const auto owned = weak.lock();
                                                         return owned && owned->part.prepare();
                                                     });
                    if (!work.completion.valid())
                    {
                        return fail("Render task submission was rejected");
                    }
                }
                else
                {
                    work.prepared = work.part.prepare();
                    if (!work.prepared)
                    {
                        return fail("Texture preparation failed");
                    }
                }
            }
            if (work.completion.valid())
            {
                if (!synchronous &&
                    work.completion.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                {
                    ++item;
                    continue;
                }
                work.prepared = work.completion.get();
                if (!work.prepared)
                {
                    return fail("Texture preparation failed");
                }
            }
            if (!budget())
            {
                break;
            }
            auto [texture, inserted] = resources.textures.try_emplace(item->first);
            if (inserted && !texture->second.create(work.part))
            {
                return fail("Cannot create GPU texture");
            }
            const auto remaining = synchronous ? std::numeric_limits<std::uint64_t>::max()
                                               : config_.upload_bytes_per_step - uploaded;
            const auto bytes = render_detail::texture_region_bytes(
                work.part, work.part.byte_offset + work.uploaded,
                std::min<std::uint64_t>(remaining, work.part.bytes - work.uploaded));
            if (!bytes)
            {
                if (!uploaded)
                {
                    return fail("Upload budget is smaller than one texture block");
                }
                break;
            }
            render_detail::PreparedTexture part;
            part.data = work.part.data;
            part.format = work.part.format;
            part.mip = work.part.mip;
            part.face = work.part.face;
            part.byte_offset = work.part.byte_offset + work.uploaded;
            part.bytes = bytes;
            part.view = work.part.view.subspan(work.uploaded, bytes);
            if (work.generation != state.generation || !texture->second.upload(part))
            {
                return fail("Cannot upload prepared texture range");
            }
            ++items;
            uploaded += bytes;
            work.uploaded += bytes;
            if (work.uploaded != work.part.bytes)
            {
                ++item;
                continue;
            }
            work.byte_offset += work.part.bytes;
            if (render_detail::texture_region_bytes(work.part, work.byte_offset, SIZE_MAX) == 0)
            {
                work.byte_offset = 0;
                if (++work.face == (work.part.data->cube_map ? 6U : 1U))
                {
                    work.face = 0;
                    ++work.mip;
                }
            }
            const bool complete = work.mip == work.part.data->mip_count;
            state.staging_bytes -= work.reserved_bytes;
            --state.active_jobs;
            work.reserved_bytes = 0;
            work.uploaded = 0;
            work.prepared = false;
            work.part = {};
            if (complete)
            {
                state.ready_textures.insert(item->first);
                item = state.textures.erase(item);
            }
            else
            {
                ++item;
            }
        }
        if (synchronous)
        {
            collect_preparation(true);
        }
    } while (synchronous && !state.textures.empty());
    for (std::size_t index = 0; index < state.ibl_sources.size(); ++index)
    {
        const auto found = resources.textures.find(state.ibl_sources[index]);
        resources.ibl_textures[index] =
            found == resources.textures.end() ? nullptr : &found->second;
    }
    state.progress.generation = state.generation;
    state.progress.staging_bytes = state.staging_bytes;
    state.progress.discovery_complete = state.dirty_nodes.empty() && !state.ibl_dirty &&
                                        state.unused_textures.empty() &&
                                        state.unused_programs.empty();
    state.progress.resources_total = static_cast<std::uint32_t>(
        state.texture_users.size() + resources.programs.size() + resources.geometries.size());
    state.progress.resources_ready =
        static_cast<std::uint32_t>(state.ready_textures.size() + resources.programs.size() -
                                   state.pending_programs.size() + resources.geometries.size());
    state.status = state.progress.discovery_complete && state.textures.empty() &&
                           state.pending_programs.empty()
                       ? RenderPrepareStatus::ready
                       : RenderPrepareStatus::preparing;
    ENGINE_PROFILE_PLOT("Render.textureStagingBytes", static_cast<double>(state.staging_bytes));
    output = state.progress;
    const bool capture_active = capture_status_ == SceneCaptureStatus::pending ||
                                hdr_capture_status_ == SceneCaptureStatus::pending;
    if (state.status == RenderPrepareStatus::ready && resources.meshes.empty() && !capture_active &&
        !render_detail::drawable_scene(*world_, camera, !extensions_.empty()))
    {
        release_draw_resources();
    }
    return state.status;
}

void RenderSystem::collect_preparation(bool wait) noexcept
{
    if (!preparation_)
    {
        return;
    }
    auto& state = *preparation_;
    std::erase_if(state.retired_work,
                  [&](auto& work)
                  {
                      if (work->completion.valid())
                      {
                          if (!wait && work->completion.wait_for(std::chrono::seconds(0)) !=
                                           std::future_status::ready)
                          {
                              return false;
                          }
                          (void)work->completion.get();
                      }
                      state.staging_bytes -= work->reserved_bytes;
                      if (work->reserved_bytes)
                      {
                          --state.active_jobs;
                      }
                      return true;
                  });
    const auto complete = [&](auto& resources)
    {
        if (resources->capture_pending &&
            static_cast<std::int32_t>(frame_number_ - resources->capture_ready_frame) >= 0)
        {
            resources->capture_pending = false;
        }
        if (resources->hdr_capture_pending &&
            static_cast<std::int32_t>(frame_number_ - resources->hdr_capture_ready_frame) >= 0)
        {
            resources->hdr_capture_pending = false;
        }
        return !resources->capture_pending && !resources->hdr_capture_pending;
    };
    do
    {
        std::erase_if(state.retired_scenes, complete);
        if (wait && !state.retired_scenes.empty())
        {
            frame_number_ = bgfx::frame();
        }
    } while (wait && !state.retired_scenes.empty());
    state.progress.staging_bytes = state.staging_bytes;
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
    if (preparation_)
    {
        auto& state = *preparation_;
        state.detach_observers();
        for (auto& [source, work] : state.textures)
        {
            state.retired_work.push_back(std::move(work));
        }
        state.textures.clear();
        state.ready_textures.clear();
        state.texture_users.clear();
        state.program_users.clear();
        state.unused_textures.clear();
        state.unused_programs.clear();
        state.pending_programs.clear();
        state.ibl_sources = {};
        state.dirty_nodes.clear();
        state.scenes.clear();
        state.discover = true;
        state.ibl_dirty = true;
        state.error.clear();
        state.status = RenderPrepareStatus::idle;
        state.progress = {};
        state.progress.generation = ++state.generation;
    }
    release_draw_resources();
    collect_preparation(false);
}

void RenderSystem::release_draw_resources() noexcept
{
    if (scene_resources_ &&
        (scene_resources_->capture_pending || scene_resources_->hdr_capture_pending))
    {
        if (!preparation_)
        {
            preparation_ = std::make_unique<RenderPreparation>();
        }
        preparation_->retired_scenes.push_back(std::move(scene_resources_));
    }
    scene_resources_.reset();
}

std::uint32_t RenderSystem::gpu_resource_count() const noexcept
{
    return scene_resources_ ? scene_resources_->resource_count() : 0;
}
} // namespace engine
