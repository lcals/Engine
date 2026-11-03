#include "plugins/runtime/effects/effect_system.hpp"
#include "runtime/scene/ecs_scene.hpp"
#include "runtime/scene/ecs_storage.hpp"

#include "plugins/runtime/effects/effect_component.hpp"
#include "plugins/runtime/effects/effect_model_loader.hpp"
#include "plugins/runtime/effects/effect_resource_path.hpp"
#include "plugins/runtime/effects/effect_resources.hpp"
#include "runtime/performance/profiling.hpp"
#include "runtime/render/render_context.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/light.hpp"
#include "runtime/scene/node.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/world/world_system.hpp"

#include <Effekseer.h>
#include <bgfxrenderer.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace engine::plugins::effects
{
namespace
{

bool has_effect_resources(const WorldSystem& world)
{
    bool found = false;
    auto query = flecs::world(world.native_world())
                     .query_builder<const ecs_detail::ComponentStorage<EffectComponent::Data>>()
                     .with<ecs_detail::Published>()
                     .build();
    query.each([&](const auto& stored) { found = found || !stored.data.effect_guid.empty(); });
    return found;
}

bool effect_transform(const Node& node, Effekseer::Matrix43& output)
{
    const auto transform = node.get_component<Transform>();
    float matrix[16]{};
    if (!transform || !transform.world_matrix(matrix))
    {
        return false;
    }
    // 引擎列向量矩阵与 Effekseer 行向量矩阵的转置表示具有相同内存顺序。
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 3; ++column)
        {
            output.Value[row][column] = matrix[row * 4 + column];
        }
    }
    return true;
}

void set_effect_light(const Scene& scene, EffekseerRenderer::Renderer& renderer)
{
    renderer.SetLightDirection({0.0F, 1.0F, 0.0F});
    renderer.SetLightColor({255, 255, 255, 255});
    renderer.SetLightAmbientColor({0, 0, 0, 255});
    if (!scene.world().refresh_scene_state())
    {
        return;
    }
    std::uint64_t selected_order = UINT64_MAX;
    auto query =
        flecs::world(scene.world().native_world())
            .query_builder<const ecs_detail::ComponentStorage<Light::Data>,
                           const ecs_detail::NodeRuntime, const ecs_detail::WorldTransform>()
            .with<ecs_detail::Published>()
            .build();
    query.each(
        [&](const auto& stored, const ecs_detail::NodeRuntime& state,
            const ecs_detail::WorldTransform& pose)
        {
            if (state.scene != &scene || !state.active || !state.visible_in_hierarchy ||
                !stored.state.flags.enabled || !stored.state.flags.visible || !pose.valid ||
                stored.data.kind != schema::LightKind::directional || state.order >= selected_order)
            {
                return;
            }
            selected_order = state.order;
            const auto& settings = stored.data;
            const auto channel = [&settings](float value)
            {
                return static_cast<std::uint8_t>(
                    std::clamp(value * settings.intensity, 0.0F, 1.0F) * 255.0F);
            };
            renderer.SetLightDirection({pose.matrix[8], pose.matrix[9], pose.matrix[10]});
            renderer.SetLightColor({channel(settings.color.r()), channel(settings.color.g()),
                                    channel(settings.color.b()), 255});
        });
}

} // namespace

struct EffectSystem::State
{
    struct CompiledEffect
    {
        ResourcePtr resource;
        detail::EffectBindings bindings;
        Effekseer::EffectRef effect;
    };

    struct Instance
    {
        std::uint64_t revision = 0;
        std::shared_ptr<CompiledEffect> compiled;
        Effekseer::Handle handle = -1;
    };

    detail::EffectResources resources;
    EffekseerRenderer::RendererRef renderer;
    Effekseer::ManagerRef manager;
    std::map<std::pair<std::string, std::uint64_t>, std::weak_ptr<CompiledEffect>> effects;
    std::map<std::uint64_t, Instance> instances;
    float time = 0.0F;
    bool prepared = false;

    ~State() noexcept
    {
        release_gpu();
    }

    void release_scene() noexcept
    {
        manager.Reset();
        instances.clear();
        effects.clear();
        resources.release_textures();
        time = 0.0F;
        prepared = false;
    }

    void release_gpu() noexcept
    {
        release_scene();
        renderer.Reset();
        // efkbgfx 程序不拥有 shader；须在 renderer 销毁后配对释放。
        resources.release_shaders();
    }

    bool initialize(const RenderContext& context)
    {
        resources.error.clear();
        if (!renderer)
        {
            auto arguments = resources.renderer_arguments(context.view_id);
            renderer = EffekseerRendererBGFX::CreateRenderer(&arguments);
            if (!renderer)
            {
                if (resources.error.empty())
                {
                    resources.error = "Cannot create Effekseer bgfx renderer";
                }
                return false;
            }
            renderer->SetMaintainGammaColorInLinearColorSpace(true);
        }
        if (!manager)
        {
            manager = Effekseer::Manager::Create(16384);
            if (!manager)
            {
                resources.error = "Cannot create Effekseer manager";
                return false;
            }
            // 调度及渲染器登记复用 Effekseer 官方 Examples 的集成方式。
            manager->SetCoordinateSystem(Effekseer::CoordinateSystem::RH);
            // SDK 按原文件名缓存会混淆不同资源的同名贴图；由引擎 GUID/代际缓存共享资源。
            manager->GetSetting()->SetIsFileCacheEnabled(false);
            manager->SetSpriteRenderer(renderer->CreateSpriteRenderer());
            manager->SetRibbonRenderer(renderer->CreateRibbonRenderer());
            manager->SetRingRenderer(renderer->CreateRingRenderer());
            manager->SetTrackRenderer(renderer->CreateTrackRenderer());
            // efkbgfx 的独立工厂会初始化模型 shader；虚方法只创建未初始化对象。
            auto arguments = resources.renderer_arguments(context.view_id);
            auto model_renderer = EffekseerRendererBGFX::CreateModelRenderer(renderer, &arguments);
            if (!model_renderer || !resources.error.empty())
            {
                if (resources.error.empty())
                {
                    resources.error = "Cannot initialize Effekseer model renderer";
                }
                manager.Reset();
                return false;
            }
            manager->SetModelRenderer(model_renderer);
            manager->SetTextureLoader(renderer->CreateTextureLoader());
            manager->SetModelLoader(Effekseer::MakeRefPtr<detail::EffectModelLoader>(
                renderer->CreateModelLoader(), resources));
        }
        return true;
    }

    std::shared_ptr<CompiledEffect> compile(const ResourcePtr& resource)
    {
        const auto key = std::make_pair(resource->info().guid, resource->generation());
        const auto existing = effects.find(key);
        if (existing != effects.end())
        {
            if (auto compiled = existing->second.lock())
            {
                return compiled;
            }
        }
        resources.error.clear();
        const schema::EffectAssetData* asset = nullptr;
        if (!detail::read_effect_asset(resource->bytes(), asset, resources.error) ||
            asset->data()->size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        {
            if (resources.error.empty())
            {
                resources.error = "Effect runtime data is too large";
            }
            return nullptr;
        }
        auto result = std::make_shared<CompiledEffect>();
        result->resource = resource;
        if (!detail::load_effect_bindings(*resource, *asset, result->bindings, resources.error))
        {
            return nullptr;
        }
        resources.loading = &result->bindings;
        result->effect =
            Effekseer::Effect::Create(manager, asset->data()->data(),
                                      static_cast<std::int32_t>(asset->data()->size()), 1.0F, u"");
        resources.loading = nullptr;
        if (!result->effect || !resources.error.empty())
        {
            if (resources.error.empty())
            {
                resources.error = "Cannot create effect: " + resource->info().path;
            }
            return nullptr;
        }
        const auto& effect = result->effect;
        const auto loaded = [](int count, const auto& get)
        {
            for (int index = 0; index < count; ++index)
            {
                if (!get(index))
                {
                    return false;
                }
            }
            return true;
        };
        if (!loaded(effect->GetColorImageCount(),
                    [&](int index) { return effect->GetColorImage(index); }) ||
            !loaded(effect->GetNormalImageCount(),
                    [&](int index) { return effect->GetNormalImage(index); }) ||
            !loaded(effect->GetDistortionImageCount(),
                    [&](int index) { return effect->GetDistortionImage(index); }) ||
            !loaded(effect->GetModelCount(), [&](int index) { return effect->GetModel(index); }))
        {
            resources.error = "Effect dependencies were not fully loaded: " + resource->info().path;
            return nullptr;
        }
        effects[key] = result;
        return result;
    }

    void stop(Instance& instance) noexcept
    {
        if (manager && instance.handle >= 0)
        {
            manager->StopEffect(instance.handle);
        }
        instance.handle = -1;
    }

    bool start(Instance& instance, EffectComponent& component)
    {
        if (!instance.compiled)
        {
            instance.compiled = compile(component.runtime()->effect_);
            if (!instance.compiled)
            {
                return false;
            }
        }
        instance.handle = manager->Play(instance.compiled->effect, 0, 0, 0);
        if (instance.handle < 0)
        {
            resources.error = "Effekseer cannot allocate another effect instance";
            return false;
        }
        return true;
    }

    bool synchronize(WorldSystem& world, double delta_time, bool world_paused)
    {
        ENGINE_PROFILE_ZONE("Effects.synchronize");
        resources.error.clear();
        std::set<std::uint64_t> alive;
        // 仅借用到本次同步返回；避免对每个实例线性查找整棵场景。
        std::vector<std::pair<EffectComponent, Instance*>> active;
        {
            const auto process =
                [&](Node node,
                    const ecs_detail::ComponentStorage<EffectComponent::Data>& stored) -> bool
            {
                const auto* scene = node.scene();
                (void)scene;
                (void)stored;

                auto component = node.get_component<EffectComponent>();
                if (!component || !component.runtime()->effect_)
                {
                    return true;
                }
                auto* source = ResourceSystem::instance();
                if (source && component.runtime()->effect_->generation() != source->generation())
                {
                    const auto settings = stored.data;
                    if (!component.set_settings(settings))
                    {
                        resources.error = "Cannot refresh effect resource: " +
                                          std::string(component.effect_path());
                        return false;
                    }
                }
                const auto id = component.instance_id();
                alive.insert(id);
                auto& instance = instances[id];
                if (!component.active())
                {
                    if (instance.handle >= 0)
                    {
                        manager->SetPaused(instance.handle, true);
                        manager->SetShown(instance.handle, false);
                    }
                    component.runtime()->playing_ = false;
                    return true;
                }
                if (instance.revision != component.runtime()->revision_)
                {
                    stop(instance);
                    instance.compiled.reset();
                    instance.revision = component.runtime()->revision_;
                }
                if (component.runtime()->awake_pending_)
                {
                    if (world_paused)
                    {
                        return true;
                    }
                    component.runtime()->requested_ = EffectComponent::Playback::playing;
                    component.runtime()->awake_pending_ = false;
                }
                if (component.runtime()->requested_ == EffectComponent::Playback::stopped)
                {
                    stop(instance);
                    instance.compiled.reset();
                    component.runtime()->playing_ = false;
                    return true;
                }
                if (instance.handle < 0 && world_paused)
                {
                    return true;
                }
                if (instance.handle < 0 && !start(instance, component))
                {
                    return false;
                }
                Effekseer::Matrix43 transform;
                if (!effect_transform(node, transform))
                {
                    resources.error = "Invalid effect emitter world transform";
                    return false;
                }
                manager->SetMatrix(instance.handle, transform);
                manager->SetShown(instance.handle, component.visible_in_hierarchy());
                manager->SetSpeed(instance.handle, stored.data.speed);
                manager->SetPaused(instance.handle,
                                   world_paused || component.runtime()->requested_ ==
                                                       EffectComponent::Playback::paused);
                active.emplace_back(component, &instance);

                return true;
            };
            auto query =
                flecs::world(world.native_world())
                    .query_builder<const ecs_detail::ComponentStorage<EffectComponent::Data>>()
                    .with<ecs_detail::Published>()
                    .build();
            bool query_success = true;
            query.each(
                [&](flecs::entity entity,
                    const ecs_detail::ComponentStorage<EffectComponent::Data>& stored)
                {
                    if (query_success)
                    {
                        query_success = process(world.find_node(entity.id()), stored);
                    }
                });
            if (!query_success)
            {
                return false;
            }
        }
        for (auto entry = instances.begin(); entry != instances.end();)
        {
            if (!alive.contains(entry->first))
            {
                stop(entry->second);
                entry = instances.erase(entry);
            }
            else
            {
                ++entry;
            }
        }
        if (!world_paused)
        {
            ENGINE_PROFILE_ZONE("Effects.managerUpdate");
            manager->Update(static_cast<float>(delta_time * 60.0));
            time += static_cast<float>(delta_time);
        }
        bool restarted = false;
        for (auto& [component, entry] : active)
        {
            auto& instance = *entry;
            if (instance.handle >= 0 && !manager->Exists(instance.handle))
            {
                instance.handle = -1;
                if (!world_paused &&
                    component.runtime()->requested_ == EffectComponent::Playback::playing &&
                    component.settings().loop)
                {
                    Effekseer::Matrix43 transform;
                    if (!start(instance, component) ||
                        !effect_transform(component.node(), transform))
                    {
                        return false;
                    }
                    manager->SetMatrix(instance.handle, transform);
                    manager->SetShown(instance.handle, component.visible_in_hierarchy());
                    manager->SetSpeed(instance.handle, component.settings().speed);
                    restarted = true;
                }
                else
                {
                    component.runtime()->requested_ = EffectComponent::Playback::stopped;
                    instance.compiled.reset();
                }
            }
            component.runtime()->playing_ =
                instance.handle >= 0 && manager->Exists(instance.handle) &&
                component.runtime()->requested_ == EffectComponent::Playback::playing;
        }
        if (restarted && !world_paused)
        {
            manager->Update(0.0F);
        }
        std::erase_if(effects, [](const auto& item) { return item.second.expired(); });
        prepared = true;
        return true;
    }
};

EffectSystem::EffectSystem(WorldSystem& world, RenderSystem& renderer) noexcept
    : world_(world), registration_target_(&renderer)
{
}

EffectSystem::~EffectSystem() noexcept
{
    shutdown();
    release_gpu();
}

bool EffectSystem::on_initialize(const EngineConfig&)
{
    auto& registry = world_.component_registry();
    if ((!registry.contains(EffectComponent::component_type) && !registry.add<EffectComponent>()) ||
        (registration_target_ && !registration_target_->add_extension(*this)))
    {
        last_error_ = "Cannot register effect component or render extension";
        return false;
    }
    registration_target_ = nullptr;
    last_error_.clear();
    return true;
}

bool EffectSystem::on_tick(double delta_time)
{
    ENGINE_PROFILE_ZONE("Effects.tick");
    if (!std::isfinite(delta_time) || delta_time < 0.0 ||
        delta_time > std::numeric_limits<float>::max() / 60.0 - pending_seconds_)
    {
        last_error_ = "Invalid effect tick duration";
        return false;
    }
    if (!has_effect_resources(world_))
    {
        release_scene();
        return true;
    }
    const bool paused = world_.paused();
    pending_seconds_ = paused ? 0.0 : pending_seconds_ + delta_time;
    if (state_ && state_->manager)
    {
        const double elapsed = std::exchange(pending_seconds_, 0.0);
        if (!state_->synchronize(world_, elapsed, paused))
        {
            last_error_ = state_->resources.error;
            return false;
        }
    }
    return true;
}

bool EffectSystem::draw(const RenderContext& context)
{
    ENGINE_PROFILE_ZONE("Effects.drawSubmit");
    if (!is_initialized() || world_.scene() != &context.scene)
    {
        last_error_ = "Effect renderer requires its current world scene";
        return false;
    }
    if (!has_effect_resources(world_))
    {
        release_scene();
        last_error_.clear();
        return true;
    }
    if (!state_)
    {
        state_ = std::make_unique<State>();
    }
    const bool paused = world_.paused();
    if (paused)
    {
        pending_seconds_ = 0.0;
    }
    if (!state_->initialize(context) ||
        ((!state_->prepared || paused) &&
         !state_->synchronize(world_, std::exchange(pending_seconds_, 0.0), paused)))
    {
        last_error_ = state_->resources.error;
        return false;
    }
    state_->prepared = false;
    auto& renderer = *state_->renderer.Get();
    Effekseer::Matrix44 camera;
    Effekseer::Matrix44 projection;
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            camera.Values[row][column] = context.view[row * 4 + column];
            projection.Values[row][column] = context.projection[row * 4 + column];
        }
    }
    renderer.SetCameraMatrix(camera);
    renderer.SetProjectionMatrix(projection);
    renderer.SetTime(state_->time);
    set_effect_light(context.scene, renderer);
    EffekseerRendererBGFX::SetOutputGamma(state_->renderer, !context.hdr);
    if (!renderer.BeginRendering())
    {
        last_error_ = "Cannot begin Effekseer rendering";
        return false;
    }
    Effekseer::Manager::DrawParameter draw;
    draw.ViewProjectionMatrix = renderer.GetCameraProjectionMatrix();
    draw.ZNear = bgfx::getCaps()->homogeneousDepth ? -1.0F : 0.0F;
    draw.ZFar = 1.0F;
    draw.CameraPosition = renderer.GetCameraPosition();
    draw.CameraFrontDirection = renderer.GetCameraFrontDirection();
    draw.IsSortingEffectsEnabled = true;
    state_->manager->Draw(draw);
    if (!renderer.EndRendering())
    {
        last_error_ = "Cannot finish Effekseer rendering";
        return false;
    }
    last_error_.clear();
    return true;
}

void EffectSystem::release_scene() noexcept
{
    // 只查询世界当前仍存活的节点；从不保存跨帧的组件指针。
    if (world_.native_world())
    {
        const auto process =
            [&](Node node,
                const ecs_detail::ComponentStorage<EffectComponent::Data>& stored) -> bool
        {
            const auto* scene = node.scene();
            (void)scene;
            (void)stored;

            if (auto effect = node.get_component<EffectComponent>())
            {
                effect.runtime()->playing_ = false;
            }

            return true;
        };
        auto query = flecs::world(world_.native_world())
                         .query_builder<const ecs_detail::ComponentStorage<EffectComponent::Data>>()
                         .with<ecs_detail::Published>()
                         .build();
        bool query_success = true;
        query.each(
            [&](flecs::entity entity,
                const ecs_detail::ComponentStorage<EffectComponent::Data>& stored)
            {
                if (query_success)
                {
                    query_success = process(world_.find_node(entity.id()), stored);
                }
            });
        (void)query_success;
    }
    if (state_)
    {
        state_->release_scene();
    }
    pending_seconds_ = 0.0;
}

void EffectSystem::release_gpu() noexcept
{
    release_scene();
    if (state_)
    {
        state_->release_gpu();
    }
    pending_seconds_ = 0.0;
}

void EffectSystem::on_shutdown() noexcept
{
    release_gpu();
    state_.reset();
    last_error_.clear();
}

const std::string& EffectSystem::last_error() const noexcept
{
    return last_error_;
}

} // namespace engine::plugins::effects
