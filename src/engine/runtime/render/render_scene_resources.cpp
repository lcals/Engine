#include "runtime/font/font_system.hpp"
#include "runtime/performance/profiling.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/render/scene_gpu_resources.hpp"
#include "runtime/render/scene_render_query.hpp"
#include "runtime/resource/resource.hpp"
#include "runtime/scene/camera_math.hpp"
#include "runtime/scene/ecs_scene.hpp"
#include "runtime/scene/ecs_storage.hpp"
#include "runtime/scene/environment_component.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/mesh_renderer_runtime.hpp"
#include "runtime/scene/planar_reflection_component.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/task/task_system.hpp"
#include "runtime/ui/ui_system.hpp"
#include "runtime/world/world_system.hpp"
#include <algorithm>
#include <bit>
#include <bx/math.h>
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

RenderPrepareStatus RenderSystem::prepare_resources(RenderPrepareProgress& output,
                                                    double max_seconds, Camera camera)
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
        state.status = RenderPrepareStatus::failed;
        return state.status;
    };
    if (!is_initialized() || (camera.instance_id() != 0 && !camera) ||
        !std::isfinite(max_seconds) || max_seconds <= 0)
    {
        return fail("Invalid render preparation request");
    }
    const auto begin = PreparationClock::now();
    const auto seconds = std::min(max_seconds, config_.prepare_seconds_per_step);
    std::uint32_t items = 0;
    std::uint64_t uploaded = 0;
    const auto budget = [&]
    {
        return items < config_.prepare_items_per_step &&
               (items == 0 ||
                std::chrono::duration<double>(PreparationClock::now() - begin).count() < seconds);
    };
    if (world_ && (!world_->scenes().empty() || scope_preparing_scene_))
    {
        if (!prepare_frame())
        {
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
    for (const auto& [id, value] : scope_preparations_)
    {
        (void)value;
        if (auto* candidate = world_ ? world_->scope_candidate(id) : nullptr)
        {
            if (std::find(scenes.begin(), scenes.end(), candidate->id()) == scenes.end())
            {
                scenes.push_back(candidate->id());
            }
        }
    }
    std::sort(scenes.begin(), scenes.end());
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
        // Shared in-flight resources survive independent scope publication.
        state.scenes = std::move(scenes);
        state.progress.generation = ++state.generation;
        state.progress.discovery_complete = false;
        state.status = RenderPrepareStatus::preparing;
        state.discover = true;
        state.ibl_dirty = true;
    }
    if (state.status == RenderPrepareStatus::failed)
    {
        state.status = RenderPrepareStatus::preparing;
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
    const auto selected_camera = camera                   ? camera
                                 : scope_preparing_scene_ ? scope_preparing_scene_->active_camera()
                                 : world_->scene()        ? world_->scene()->active_camera()
                                                          : Camera{};
    if (!scene_resources_ && state.status == RenderPrepareStatus::ready &&
        state.dirty_nodes.empty() && !state.ibl_dirty && !state.discover &&
        !render_detail::drawable_scene(*world_, camera, !extensions_.empty(),
                                       scope_preparing_scene_))
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
    const auto* target_scene = scope_preparing_scene_ ? scope_preparing_scene_ : world_->scene();
    const auto target_id = target_scene ? render_detail::target_scope(*target_scene) : 0;
    resources.select_targets(target_id);
    auto& targets = resources.targets();
    if (targets.camera_instance != selected_camera.instance_id())
    {
        targets.camera_instance = selected_camera.instance_id();
        targets.ibl_dirty = true;
        state.status = RenderPrepareStatus::preparing;
    }
    if (budget())
    {
        const auto retired =
            std::find_if(resources.pass_targets.begin(), resources.pass_targets.end(),
                         [&](const auto& entry)
                         {
                             return entry.first != target_id &&
                                    (!world_->scope_candidate({world_->epoch(), entry.first}) ||
                                     (target_scene == world_->scene() &&
                                      world_->scope_candidate({world_->epoch(), entry.first}) ==
                                          world_->scene()));
                         });
        if (retired != resources.pass_targets.end())
        {
            ++items;
            for (auto* source : retired->second->ibl_sources)
            {
                if (source && --state.texture_users[source] == 0)
                {
                    state.unused_textures.insert(source);
                }
            }
            resources.pass_targets.erase(retired);
        }
    }
    const auto includes = [&](std::uint64_t id)
    {
        flecs::world ecs(world_->native_world());
        const auto entity = ecs.entity(id);
        if (!entity.is_alive())
        {
            return false;
        }
        const auto* runtime = entity.try_get<ecs_detail::NodeRuntime>();
        return runtime && (target_scene == world_->scene() ? entity.has<ecs_detail::Published>()
                                                           : runtime->scope_scene == target_scene);
    };
    const auto required_texture = [&](const Resource* source)
    {
        if (std::find(targets.ibl_sources.begin(), targets.ibl_sources.end(), source) !=
            targets.ibl_sources.end())
        {
            return true;
        }
        return std::any_of(resources.meshes.begin(), resources.meshes.end(),
                           [&](const auto& mesh)
                           {
                               return includes(mesh.node_id) &&
                                      std::find(mesh.textures.begin(), mesh.textures.end(),
                                                source) != mesh.textures.end();
                           });
    };
    const auto required_program = [&](const schema::ShaderDataT* source)
    {
        return std::any_of(resources.meshes.begin(), resources.meshes.end(),
                           [&](const auto& mesh)
                           {
                               return includes(mesh.node_id) &&
                                      std::find(mesh.programs.begin(), mesh.programs.end(),
                                                source) != mesh.programs.end();
                           });
    };

    if (state.ibl_dirty)
    {
        for (auto& [id, target] : resources.pass_targets)
        {
            (void)id;
            target->ibl_dirty = true;
        }
        state.ibl_dirty = false;
    }
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
                .with<ecs_detail::NodeRuntime>()
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
                                      .with<ecs_detail::NodeRuntime>()
                                      .event<Node>()
                                      .run(
                                          [pending, dirty](flecs::iter& it)
                                          {
                                              while (it.next())
                                              {
                                                  pending->ibl_dirty = true;
                                                  for (auto row : it)
                                                  {
                                                      std::vector<flecs::entity> descendants;
                                                      const auto mark = [&](flecs::entity entity)
                                                      {
                                                          dirty(entity.id());
                                                          entity.children(
                                                              [&](flecs::entity child)
                                                              { descendants.push_back(child); });
                                                      };
                                                      mark(it.entity(row));
                                                      while (!descendants.empty())
                                                      {
                                                          const auto child = descendants.back();
                                                          descendants.pop_back();
                                                          mark(child);
                                                      }
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
                         .with<ecs_detail::NodeRuntime>()
                         .build();
        query.each(
            [&](flecs::entity entity, const auto&)
            {
                const auto* runtime = entity.try_get<ecs_detail::NodeRuntime>();
                bool included = entity.has<ecs_detail::Published>();
                for (const auto& [scope, value] : scope_preparations_)
                {
                    (void)value;
                    const auto* candidate = world_->scope_candidate(scope);
                    included = included || (runtime && runtime->scope_scene == candidate);
                }
                if (included)
                {
                    state.dirty_nodes.insert(entity.id());
                }
            });
        state.discover = false;
    }
    auto* loader = world_->resource_loader();
    while (budget())
    {
        const auto pending_node =
            std::find_if(state.dirty_nodes.begin(), state.dirty_nodes.end(), [&](auto id)
                         { return includes(id) || !ecs_is_alive(world_->native_world(), id); });
        if (pending_node == state.dirty_nodes.end())
        {
            break;
        }
        ++items;
        const auto id = *pending_node;
        state.dirty_nodes.erase(pending_node);
        auto node = world_->find_node(id);
        if (!node)
        {
            for (const auto& [scope, value] : scope_preparations_)
            {
                (void)value;
                auto* candidate = world_->scope_candidate(scope);
                if (candidate && (node = candidate->find_node(id)))
                {
                    break;
                }
            }
        }
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
        if (mesh && !mesh->materials.empty() && !resources.retained_meshes.contains(id))
        {
            for (auto* source : mesh->textures)
            {
                ++state.texture_users[source];
            }
            for (auto* source : mesh->programs)
            {
                ++state.program_users[source];
            }
            resources.retained_meshes.emplace(id, std::move(*mesh));
        }
        const bool visible = renderer && renderer.active() && renderer.visible_in_hierarchy();
        if (!visible)
        {
            if (const auto retained = resources.retained_meshes.find(id);
                retained != resources.retained_meshes.end())
            {
                for (auto* source : retained->second.textures)
                {
                    if (--state.texture_users[source] == 0)
                    {
                        state.unused_textures.insert(source);
                    }
                }
                for (auto* source : retained->second.programs)
                {
                    if (--state.program_users[source] == 0)
                    {
                        state.unused_programs.insert(source);
                    }
                }
                resources.retained_meshes.erase(retained);
            }
            state.pending_materials.erase(id);
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
            const auto* runtime =
                flecs::world(world_->native_world()).entity(id).try_get<MeshRendererRuntime>();
            if (!runtime || !mesh->update_deformation(*runtime, resources.skinning))
            {
                return fail("Cannot prepare mesh deformation");
            }
            mesh->textures.clear();
            mesh->programs.clear();
            mesh->materials.clear();
            state.pending_materials.insert(id);
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
            for (const auto& lod : data->lods)
            {
                for (const auto& range : lod->submeshes)
                {
                    slots.insert(range.material_slot());
                }
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
    if (targets.ibl_dirty && budget())
    {
        ++items;
        if (!resources.collect_ibl(
                scope_preparing_scene_ ? scope_preparing_scene_ : world_->scene(), state))
        {
            return fail("Invalid IBL resource binding");
        }
        targets.ibl_dirty = false;
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
        state.decoded_blocks.erase(source);
        state.uploaded_bytes.erase(source);
        state.upload_total.erase(source);
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
    while (budget())
    {
        const auto pending_program = std::find_if(state.pending_programs.begin(),
                                                  state.pending_programs.end(), required_program);
        if (pending_program == state.pending_programs.end())
        {
            break;
        }
        ++items;
        const auto source = *pending_program;
        auto& program = resources.programs.find(source)->second;
        auto retained = std::move(program.resource);
        if (!program.create(*source))
        {
            return fail("Cannot create GPU program");
        }
        program.resource = std::move(retained);
        state.pending_programs.erase(pending_program);
    }
    // 每个工作项拥有稳定源获取。预算计入正在执行及已完成等待上传的缓冲。
    {
        for (auto item = state.textures.begin(); item != state.textures.end() && budget();)
        {
            if (!required_texture(item->first))
            {
                ++item;
                continue;
            }
            auto& work = *item->second;
            if (uploaded >= config_.upload_bytes_per_step)
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
                if (!tasks_ && !native)
                {
                    return fail("Incremental Basis preparation requires a render task domain");
                }
                state.upload_total[item->first] += work.part.bytes;
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
                if (work.completion.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
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
            const auto remaining = config_.upload_bytes_per_step - uploaded;
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
            if (!texture->second.upload(part))
            {
                return fail("Cannot upload prepared texture range");
            }
            ++items;
            uploaded += bytes;
            state.uploaded_bytes[item->first] += bytes;
            work.uploaded += bytes;
            if (work.uploaded != work.part.bytes)
            {
                ++item;
                continue;
            }
            ++state.decoded_blocks[item->first];
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
    }
    for (std::size_t index = 0; index < targets.ibl_sources.size(); ++index)
    {
        const auto found = resources.textures.find(targets.ibl_sources[index]);
        resources.targets().ibl_textures[index] =
            found == resources.textures.end() ? nullptr : &found->second;
    }
    while (budget())
    {
        const auto pending_material = std::find_if(
            state.pending_materials.begin(), state.pending_materials.end(),
            [&](auto id)
            {
                if (!includes(id) || state.dirty_nodes.contains(id))
                {
                    return false;
                }
                const auto mesh =
                    std::find_if(resources.meshes.begin(), resources.meshes.end(),
                                 [&](const auto& value) { return value.node_id == id; });
                return mesh != resources.meshes.end() &&
                       std::none_of(mesh->textures.begin(), mesh->textures.end(),
                                    [&](auto source) { return state.textures.contains(source); }) &&
                       std::none_of(mesh->programs.begin(), mesh->programs.end(), [&](auto source)
                                    { return state.pending_programs.contains(source); });
            });
        if (pending_material == state.pending_materials.end())
        {
            break;
        }
        ++items;
        const auto id = *pending_material;
        const auto node =
            target_scene == world_->scene() ? world_->find_node(id) : target_scene->find_node(id);
        const auto renderer = node.get_component<MeshRenderer>();
        const auto found = resources.mesh_index.find({epoch, id, renderer.instance_id()});
        if (!renderer || found == resources.mesh_index.end())
        {
            return fail("Material owner changed during preparation");
        }
        auto& bindings = resources.meshes[found->second].materials;
        bindings.resize(renderer.material_count());
        std::set<std::size_t> used_slots;
        const auto* data = renderer.mesh_data();
        if (data->submeshes.empty())
        {
            used_slots.insert(0);
        }
        for (const auto& range : data->submeshes)
        {
            used_slots.insert(range.material_slot());
        }
        for (const auto& lod : data->lods)
        {
            for (const auto& range : lod->submeshes)
            {
                used_slots.insert(range.material_slot());
            }
        }
        for (std::size_t slot = 0; slot < bindings.size(); ++slot)
        {
            const auto material = renderer.material(slot);
            const auto program = resources.programs.find(material.shader_data());
            if (!used_slots.contains(slot))
            {
                // 未被子网格引用的槽仍参与原有全不透明/LOD 判断，但不分配 GPU 资源。
                bindings[slot].blend = material.blend_mode();
                continue;
            }
            if (program == resources.programs.end() ||
                !program->second.prepare_material(material, resources.textures, bindings[slot]))
            {
                return fail("Cannot prepare material binding");
            }
        }
        state.pending_materials.erase(pending_material);
        if (const auto retained = resources.retained_meshes.find(id);
            retained != resources.retained_meshes.end())
        {
            for (auto* source : retained->second.textures)
            {
                if (--state.texture_users[source] == 0)
                {
                    state.unused_textures.insert(source);
                }
            }
            for (auto* source : retained->second.programs)
            {
                if (--state.program_users[source] == 0)
                {
                    state.unused_programs.insert(source);
                }
            }
            resources.retained_meshes.erase(retained);
        }
    }
    // 目标按现有准备预算逐项推进；完成后稳定描述只做比较，不重新创建。
    RenderSettings rendering;
    if (!resolve_settings(rendering))
    {
        return fail("Cannot resolve render target settings");
    }
    const bool drawable = render_detail::drawable_scene(*world_, camera, !extensions_.empty(),
                                                        scope_preparing_scene_);
    if (!drawable && hdr_capture_status_ == SceneCaptureStatus::pending &&
        !resources.hdr_capture_pending)
    {
        cancel_hdr_capture();
    }
    std::array<Node, 2> plane_nodes{};
    std::array<std::uint16_t, 2> plane_widths{}, plane_heights{};
    std::vector<std::uint64_t> description{
        drawable,
        width_,
        height_,
        config_.msaa_samples,
        rendering.bloom_enabled,
        rendering.ao_enabled,
        rendering.ssr_enabled,
        std::bit_cast<std::uint32_t>(rendering.ssr_resolution_scale)};
    if (drawable)
    {
        float camera_world[16]{}, camera_view[16]{}, projection[16]{};
        CameraSettings lens;
        const auto transform = selected_camera.node().get_component<Transform>();
        if (!transform || !transform.world_matrix(camera_world) ||
            !selected_camera.get_settings(lens) ||
            !camera_detail::view_matrix(camera_world, camera_view) ||
            !camera_detail::projection_matrix(lens, static_cast<float>(width_) / height_,
                                              bgfx::getCaps()->homogeneousDepth, projection))
        {
            return fail("Cannot read render target observer");
        }
        bx::mtxInverse(camera_world, camera_view);
        std::vector<std::pair<float, Node>> planes;
        auto query = flecs::world(world_->native_world())
                         .query_builder<
                             const ecs_detail::ComponentStorage<PlanarReflectionComponent::Data>>()
                         .build();
        query.each(
            [&](flecs::entity entity, const auto&)
            {
                auto node = render_detail::scope_node(*target_scene, entity.id());
                const auto plane = node.get_component<PlanarReflectionComponent>();
                const auto mesh = node.get_component<MeshRenderer>();
                const auto pose = node.get_component<Transform>();
                float matrix[16]{};
                if (plane && plane.active() && plane.visible_in_hierarchy() && mesh &&
                    mesh.active() && mesh.visible_in_hierarchy() &&
                    selected_camera.mesh_visible(mesh.instance_id()) && pose &&
                    pose.world_matrix(matrix))
                {
                    planes.emplace_back(std::hypot(matrix[12] - camera_world[12],
                                                   matrix[13] - camera_world[13],
                                                   matrix[14] - camera_world[14]),
                                        node);
                }
            });
        std::sort(
            planes.begin(), planes.end(), [](const auto& a, const auto& b)
            { return a.first == b.first ? a.second.id() < b.second.id() : a.first < b.first; });
        for (std::size_t index = 0; index < plane_nodes.size(); ++index)
        {
            if (index < std::min<std::size_t>(planes.size(), config_.maximum_planar_reflections))
            {
                PlanarReflectionSettings options;
                plane_nodes[index] = planes[index].second;
                if (!plane_nodes[index].get_component<PlanarReflectionComponent>().get_settings(
                        options))
                {
                    return fail("Cannot read reflection target settings");
                }
                float plane_world[16]{};
                if (!plane_nodes[index].get_component<Transform>().world_matrix(plane_world))
                {
                    return fail("Cannot read reflection transform");
                }
                if (!resources.targets().planar[index].mirror(plane_world, camera_world, projection,
                                                              options.clip_bias))
                {
                    plane_nodes[index] = {};
                    continue;
                }
                plane_widths[index] = static_cast<std::uint16_t>(
                    std::max(1.0F, std::ceil(width_ * options.resolution_scale)));
                plane_heights[index] = static_cast<std::uint16_t>(
                    std::max(1.0F, std::ceil(height_ * options.resolution_scale)));
            }
        }
    }
    for (std::size_t index = 0; index < plane_nodes.size(); ++index)
    {
        description.push_back(plane_nodes[index].id());
        description.push_back(plane_widths[index]);
        description.push_back(plane_heights[index]);
    }
    const bool capture_active = capture_status_ == SceneCaptureStatus::pending;
    description.push_back(hdr_capture_status_ == SceneCaptureStatus::pending);
    description.push_back(capture_active);
    description.push_back(capture_active ? capture_request_.width : 0);
    description.push_back(capture_active ? capture_request_.height : 0);
    if (targets.description != description)
    {
        targets.description = std::move(description);
        targets.stage = 0;
    }
    constexpr std::size_t target_steps = 10;
    while (targets.stage < target_steps && budget())
    {
        ++items;
        bool ready = true;
        const auto width = static_cast<std::uint16_t>(width_);
        const auto height = static_cast<std::uint16_t>(height_);
        switch (targets.stage)
        {
        case 0:
            ready = !drawable || resources.targets().shadow.prepare(config_.shadow_atlas_size);
            break;
        case 1:
            ready = !drawable || resources.targets().surface_pass.prepare(width, height);
            break;
        case 2:
            ready = !drawable || resources.targets().hdr_post_process.prepare(
                                     width, height, rendering.bloom_enabled, config_.msaa_samples);
            break;
        case 3:
            if (drawable && rendering.ao_enabled)
            {
                ready = resources.targets().ambient_occlusion.prepare(width, height);
            }
            else
            {
                resources.targets().ambient_occlusion.release();
            }
            break;
        case 4:
            ready = !drawable || resources.targets().reflections.prepare(width, height, rendering);
            break;
        case 5:
            ready = !drawable || resources.targets().clusters[2].prepare(config_, width, height);
            break;
        case 6:
        case 7:
        {
            const auto index = targets.stage - 6;
            auto& target = resources.targets().planar[index];
            if (plane_nodes[index])
            {
                ready = target.prepare(plane_widths[index], plane_heights[index],
                                       config_.msaa_samples) &&
                        resources.targets().clusters[index].prepare(config_, plane_widths[index],
                                                                    plane_heights[index]);
                target.node_id = plane_nodes[index].id();
            }
            else
            {
                target.release();
                resources.targets().clusters[index].release();
            }
            break;
        }
        case 8:
            ready = !capture_active ||
                    (resources.targets().shadow.prepare(config_.shadow_atlas_size) &&
                     resources.targets().clusters[3].prepare(config_, capture_request_.width,
                                                             capture_request_.height) &&
                     (resources.capture_pending || resources.prepare_capture(capture_request_)));
            break;
        case 9:
            if (hdr_capture_status_ == SceneCaptureStatus::pending &&
                !resources.hdr_capture_pending)
            {
                constexpr auto flags = BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK;
                ready = drawable &&
                        bgfx::isTextureValid(0, false, 1, bgfx::TextureFormat::RGBA16F, flags);
                if (ready)
                {
                    if (bgfx::isValid(resources.hdr_capture_readback))
                    {
                        bgfx::destroy(resources.hdr_capture_readback);
                    }
                    resources.hdr_capture_readback = bgfx::createTexture2D(
                        width, height, false, 1, bgfx::TextureFormat::RGBA16F, flags);
                    ready = bgfx::isValid(resources.hdr_capture_readback);
                    resources.hdr_capture_width = width;
                    resources.hdr_capture_height = height;
                    resources.hdr_capture_pixels.resize(static_cast<std::size_t>(width) * height *
                                                        4);
                }
            }
            break;
        }
        if (!ready)
        {
            return fail("Cannot prepare render pass target");
        }
        ++targets.stage;
    }
    state.progress.generation = state.generation;
    state.progress.staging_bytes = state.staging_bytes;
    state.progress.discovery_complete =
        std::none_of(state.dirty_nodes.begin(), state.dirty_nodes.end(), includes) &&
        !targets.ibl_dirty;
    state.progress.instances_total = state.progress.instances_ready = 0;
    state.progress.programs_total = state.progress.programs_ready = 0;
    state.progress.texture_jobs = 0;
    state.progress.decoded_blocks = state.progress.upload_bytes = state.progress.upload_total = 0;
    for (const auto& mesh : resources.meshes)
    {
        if (includes(mesh.node_id))
        {
            ++state.progress.instances_total;
            state.progress.instances_ready += !state.dirty_nodes.contains(mesh.node_id) &&
                                              !state.pending_materials.contains(mesh.node_id);
        }
    }
    for (const auto& [program, gpu] : resources.programs)
    {
        if (required_program(program))
        {
            (void)gpu;
            ++state.progress.programs_total;
            state.progress.programs_ready += !state.pending_programs.contains(program);
        }
    }
    std::uint32_t texture_total = 0, texture_ready = 0;
    for (const auto& [source, users] : state.texture_users)
    {
        if (users && required_texture(source))
        {
            ++texture_total;
            texture_ready += state.ready_textures.contains(source);
            const auto pending = state.textures.find(source);
            state.progress.texture_jobs +=
                pending != state.textures.end() && pending->second->reserved_bytes != 0;
            state.progress.decoded_blocks += state.decoded_blocks[source];
            state.progress.upload_bytes += state.uploaded_bytes[source];
            state.progress.upload_total += state.upload_total[source];
        }
    }
    state.progress.resources_total =
        state.progress.instances_total + state.progress.programs_total + texture_total;
    state.progress.resources_ready =
        state.progress.instances_ready + state.progress.programs_ready + texture_ready;
    state.status =
        state.progress.discovery_complete &&
                std::none_of(state.textures.begin(), state.textures.end(),
                             [&](const auto& value) { return required_texture(value.first); }) &&
                std::none_of(state.pending_programs.begin(), state.pending_programs.end(),
                             required_program) &&
                std::none_of(state.pending_materials.begin(), state.pending_materials.end(),
                             includes) &&
                targets.stage == target_steps
            ? RenderPrepareStatus::ready
            : RenderPrepareStatus::preparing;
    ENGINE_PROFILE_PLOT("Render.textureStagingBytes", static_cast<double>(state.staging_bytes));
    output = state.progress;
    const bool any_capture_active = capture_status_ == SceneCaptureStatus::pending ||
                                    hdr_capture_status_ == SceneCaptureStatus::pending;
    if (state.status == RenderPrepareStatus::ready && resources.meshes.empty() &&
        !any_capture_active &&
        !render_detail::drawable_scene(*world_, camera, !extensions_.empty(),
                                       scope_preparing_scene_))
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
    scope_frames_.clear();
    for (auto& [id, record] : scope_preparations_)
    {
        (void)id;
        record.snapshot.state = SceneWorkState::queued;
        record.snapshot.stages.clear();
        record.completed = 0;
        record.started_ns = 0;
    }
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
        state.decoded_blocks.clear();
        state.uploaded_bytes.clear();
        state.upload_total.clear();
        state.texture_users.clear();
        state.program_users.clear();
        state.unused_textures.clear();
        state.unused_programs.clear();
        state.pending_programs.clear();
        state.pending_materials.clear();
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
