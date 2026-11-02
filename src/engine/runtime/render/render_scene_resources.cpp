#include "runtime/render/render_system.hpp"

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

bool RenderSystem::prepare_scene(const Camera* camera)
{
    ENGINE_PROFILE_ZONE("Render.prepareSceneResources");
    if (!is_initialized())
    {
        return false;
    }
    RenderSettings current_settings;
    if (!resolve_settings(current_settings))
    {
        return false;
    }
    const Scene* scene = world_ != nullptr ? world_->scene() : nullptr;
    if (scene == nullptr || scene->id() != scene_id_)
    {
        if (scene_id_ != 0)
        {
            release_extensions_scene();
        }
        // UI 可能已在玩法初始化中准备，切换绘制资源不销毁新的 View。
        release_draw_resources();
        if (fonts_)
        {
            fonts_->release_scene();
        }
        if (ui_ && !ui_->synchronize())
        {
            return false;
        }
    }
    if (scene == nullptr)
    {
        return true;
    }
    if (!render_detail::drawable_scene(*scene, camera, !extensions_.empty()))
    {
        if (scene_resources_)
        {
            release_draw_resources();
        }
        scene_id_ = scene->id();
        return true;
    }
    const auto lights = std::count_if(scene->nodes().begin(), scene->nodes().end(),
                                      [](const auto& node)
                                      {
                                          const auto* light = node->template get_component<Light>();
                                          return light != nullptr && light->enabled();
                                      });
    if (lights > static_cast<std::ptrdiff_t>(render_detail::maximum_lights))
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Scene exceeds the limit of %zu active lights",
                     render_detail::maximum_lights);
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
        scene_id_ = scene->id();
    }
    auto& resources = *scene_resources_;
    if (!resources.prepare_ibl(*scene, config_.texture_staging_bytes))
    {
        release_scene();
        return false;
    }
    auto& programs = resources.programs;
    auto& meshes = resources.meshes;
    auto& textures = resources.textures;
    std::set<std::string> needed_programs;
    std::set<std::uint64_t> needed_meshes;
    std::set<const Resource*> needed_textures;
    std::vector<render_detail::PreparedTexture> missing_textures;
    bool needs_skinning = false;
    for (const auto& node : scene->nodes())
    {
        const auto* renderer = node->get_component<MeshRenderer>();
        if (renderer == nullptr || !renderer->enabled() || !renderer->visible())
        {
            continue;
        }
        const auto* data = renderer->mesh_data();
        if (data == nullptr)
        {
            release_scene();
            return false;
        }
        needed_meshes.insert(node->id());
        if (!renderer->skinning_palette().empty())
        {
            needs_skinning = true;
            if (!resources.skinning.prepare())
            {
                release_scene();
                return false;
            }
        }
        auto mesh = std::find_if(meshes.begin(), meshes.end(),
                                 [&node](const auto& item) { return item.node_id == node->id(); });
        if (mesh == meshes.end())
        {
            meshes.emplace_back();
            mesh = std::prev(meshes.end());
        }
        if (mesh->instance_id != renderer->mesh_instance_id() ||
            mesh->revision != renderer->mesh_revision())
        {
            ENGINE_PROFILE_ZONE("Render.rebuildMeshResource");
            const auto source = renderer->mesh_resource();
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
                if (!geometry->create(*renderer))
                {
                    release_scene();
                    return false;
                }
                if (source)
                {
                    resources.geometries[source.get()] = geometry;
                }
            }
            if (!mesh->create(*renderer, std::move(geometry)))
            {
                release_scene();
                return false;
            }
        }
        if (!mesh->update_deformation(*renderer, resources.skinning))
        {
            release_scene();
            return false;
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
            const auto* material = renderer->material(slot);
            if (material == nullptr)
            {
                release_scene();
                return false;
            }
            const std::string path(material->shader_path());
            const auto* shader = material->shader_data();
            if (shader == nullptr)
            {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Unknown shader resource: %s", path.c_str());
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
                        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Cannot prepare shader resource: %s",
                                     path.c_str());
                        release_scene();
                        return false;
                    }
                }
            }
            for (const auto& sampler : shader->samplers)
            {
                const auto& source = material->texture(sampler->name);
                if (!source || source->texture() == nullptr)
                {
                    release_scene();
                    return false;
                }
                if (needed_textures.insert(source.get()).second &&
                    std::none_of(textures.begin(), textures.end(),
                                 [&source](const auto& item) { return item.resource == source; }))
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
            const bool ready = prepared.valid()
                                   ? prepared.get()
                                   : std::all_of(missing_textures.begin() + begin,
                                                 missing_textures.begin() + end,
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
    return true;
}

void RenderSystem::release_scene() noexcept
{
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
    scene_resources_.reset();
    scene_id_ = 0;
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
