#include "plugins/runtime/effects/codecs/effect_resource_codec.hpp"
#include "resource_services.hpp"
#include "runtime/resource/virtual_file_system.hpp"
#include "scene_snapshot.hpp"

#include "plugins/runtime/effects/effect_component.hpp"
#include "plugins/runtime/effects/effect_system.hpp"
#include "resource_fixture.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/render/render_context.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/schema/effect_generated.h"
#include "runtime/utils/path_utils.hpp"
#include "runtime/window/window_system.hpp"
#include "runtime/world/world_system.hpp"

#include <Effekseer/IO/Effekseer.EfkEfcFactory.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <bgfx/bgfx.h>
#include <flatbuffers/flatbuffers.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <limits>
#include <map>
#include <string>
#include <string_view>

namespace
{

using namespace engine;
using plugins::effects::EffectComponent;
using plugins::effects::EffectSystem;

bool expect(bool result, std::string_view message)
{
    if (!result)
    {
        std::fprintf(stderr, "Effects runtime: %.*s\n", static_cast<int>(message.size()),
                     message.data());
    }
    return result;
}

bool add_resource(test_support::ResourceFixture& fixture, std::string_view path,
                  schema::AssetKind kind, ByteView payload, std::span<const Guid> dependencies)
{
    auto entry = std::make_unique<schema::AssetEntryT>();
    char guid[37]{};
    std::snprintf(guid, sizeof(guid), "20000000-0000-4000-8000-%012zu",
                  fixture.catalog.entries.size() + 1);
    entry->guid = guid;
    entry->path = path;
    entry->kind = kind;
    entry->dependencies.assign(dependencies.begin(), dependencies.end());
    std::sort(entry->dependencies.begin(), entry->dependencies.end());
    ByteBuffer bytes;
    if (!ResourceSystem::encode_resource(entry->guid, entry->kind, payload, bytes,
                                         entry->header_size))
    {
        return false;
    }
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto value : bytes)
    {
        hash = (hash ^ std::to_integer<std::uint8_t>(value)) * 1099511628211ULL;
    }
    char digest[17]{};
    std::snprintf(digest, sizeof(digest), "%016llx", static_cast<unsigned long long>(hash));
    entry->content_hash = digest;
    entry->content_size = bytes.size();
    entry->location = std::make_unique<schema::FileLocationT>();
    entry->location->mount = "content";
    entry->location->path = "gen/" + entry->guid;
    entry->location->length = bytes.size();
    std::ofstream stream(fixture.root / entry->location->path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    stream.close();
    if (!stream)
    {
        return false;
    }
    fixture.catalog.entries.push_back(std::move(entry));
    return true;
}

bool add_effect(test_support::ResourceFixture& fixture, std::string_view path,
                const schema::EffectAssetDataT& effect, std::span<const Guid> dependencies)
{
    flatbuffers::FlatBufferBuilder builder;
    schema::FinishEffectAssetDataBuffer(builder, schema::EffectAssetData::Pack(builder, &effect));
    const ByteView payload(reinterpret_cast<const std::byte*>(builder.GetBufferPointer()),
                           builder.GetSize());
    return add_resource(fixture, path, schema::AssetKind::effect, payload, dependencies);
}

bool prepare_effect(test_support::ResourceFixture& fixture, ByteView source,
                    const Effekseer::EfkEfcProperty& properties, std::string_view folder,
                    std::array<std::uint8_t, 4> color,
                    const std::map<std::u16string, ByteBuffer>& models = {})
{
    schema::EffectAssetDataT effect;
    effect.data.assign(reinterpret_cast<const std::uint8_t*>(source.data()),
                       reinterpret_cast<const std::uint8_t*>(source.data()) + source.size());
    std::vector<Guid> dependencies;
    std::map<std::u16string, Guid> paths;
    for (const auto& path : properties.GetColorImages())
    {
        if (paths.contains(path))
        {
            continue;
        }
        schema::TextureDataT texture;
        texture.path = std::string(folder) + "/texture-" + std::to_string(paths.size());
        texture.width = 1;
        texture.height = 1;
        texture.encoding = schema::TextureEncoding::rgba8;
        texture.format = schema::TextureFormat::rgba8;
        texture.srgb = true;
        texture.data.assign(color.begin(), color.end());
        if (!fixture.add(texture))
        {
            return false;
        }
        const auto& guid = fixture.catalog.entries.back()->guid;
        paths.emplace(path, guid);
        dependencies.push_back(guid);
        auto binding = std::make_unique<schema::EffectResourceBindingT>();
        binding->path = PathUtils::to_utf8(std::filesystem::path(path).lexically_normal());
        std::replace(binding->path.begin(), binding->path.end(), '\\', '/');
        binding->guid = guid;
        binding->kind = schema::AssetKind::texture;
        effect.bindings.push_back(std::move(binding));
    }
    for (const auto& path : properties.GetModels())
    {
        const auto found = models.find(path);
        const auto logical = std::string(folder) + "/model-" + std::to_string(paths.size());
        if (found == models.end() || found->second.empty() || paths.contains(path) ||
            !add_resource(fixture, logical, schema::AssetKind::source, found->second, {}))
        {
            return false;
        }
        const auto& guid = fixture.catalog.entries.back()->guid;
        paths.emplace(path, guid);
        dependencies.push_back(guid);
        auto binding = std::make_unique<schema::EffectResourceBindingT>();
        binding->path = PathUtils::to_utf8(std::filesystem::path(path).lexically_normal());
        std::replace(binding->path.begin(), binding->path.end(), '\\', '/');
        binding->guid = guid;
        binding->kind = schema::AssetKind::source;
        effect.bindings.push_back(std::move(binding));
    }
    return add_effect(fixture, std::string(folder) + "/effect", effect, dependencies);
}

bool advance(WindowSystem& windows, RenderSystem& renderer, EffectSystem& effects, int count = 4)
{
    int frames = 0;
    for (int attempt = 0; attempt < 200 && frames < count; ++attempt)
    {
        if (!expect(windows.tick(1.0 / 60.0) && !windows.exit_requested(), "poll effect window"))
        {
            return false;
        }
        if (!windows.can_render())
        {
            SDL_Delay(5);
            continue;
        }
        if (!expect(effects.tick(1.0 / 60.0), effects.last_error()) ||
            !expect(renderer.render(), effects.last_error()))
        {
            return false;
        }
        ++frames;
    }
    return expect(frames == count, "effect frames become drawable");
}

} // namespace

int main(int argc, char** argv)
{
    EngineConfig config;
    config.width = 640;
    config.height = 360;
    config.window_title = "Effects runtime integration";
    test_support::ResourceServices services;
    ResourceSystem& resources = services.core;
    WorldSystem world{&services.loader};
    WindowSystem windows;
    RenderSystem renderer(windows, &world);
    EffectSystem effects(world, renderer);
    test_support::ResourceFixture fixture;
    ByteBuffer source;
    ByteBuffer model_source;
    ByteBuffer model_bytes;
    Effekseer::EfkEfcProperty properties;
    Effekseer::EfkEfcProperty model_properties;
    if (!expect(argc == 2 &&
                    VirtualFileSystem::read_file(
                        PathUtils::from_utf8(argv[1]) /
                            "projects/showcase/assets/effects/laser/laser.efkefc",
                        source) &&
                    !source.empty() &&
                    properties.Load(source.data(), static_cast<std::int32_t>(source.size())) &&
                    !properties.GetColorImages().empty() && properties.GetNormalImages().empty() &&
                    properties.GetDistortionImages().empty() && properties.GetModels().empty(),
                "read the official sprite fixture and its declared color textures") ||
        !expect(
            VirtualFileSystem::read_file(PathUtils::from_utf8(argv[1]) /
                                             "projects/showcase/assets/effects/arrow/arrow.efkefc",
                                         model_source) &&
                !model_source.empty() &&
                model_properties.Load(model_source.data(),
                                      static_cast<std::int32_t>(model_source.size())) &&
                model_properties.GetModels().size() == 1 &&
                !model_properties.GetColorImages().empty() &&
                model_properties.GetNormalImages().empty() &&
                model_properties.GetDistortionImages().empty() &&
                VirtualFileSystem::read_file(
                    PathUtils::from_utf8(argv[1]) /
                        "projects/showcase/assets/effects/arrow/models/arrow.efkmodel",
                    model_bytes) &&
                !model_bytes.empty(),
            "read official Simple_Model_UV and its actual model geometry"))
    {
        return 1;
    }
    const std::map<std::u16string, ByteBuffer> models{
        {model_properties.GetModels().front(), std::move(model_bytes)}};
    if (!expect((resources.add<engine::plugins::effects::EffectResourceCodec>() &&
                 services.initialize(config)) &&
                    world.initialize(config) && effects.initialize(config),
                "initialize effect ownership and registration") ||
        !expect(prepare_effect(fixture, source, properties, "red", {255, 40, 40, 255}) &&
                    prepare_effect(fixture, source, properties, "green", {40, 255, 40, 255}) &&
                    prepare_effect(fixture, model_source, model_properties, "model",
                                   {90, 130, 255, 255}, models) &&
                    add_effect(fixture, "invalid/effect", {}, {}) &&
                    (fixture.publish(resources) && services.prepare_builtins() &&
                     [&]
                     {
                         for (const auto& entry : fixture.catalog.entries)
                         {
                             if (entry->path != "invalid/effect" && !services.load(entry->path))
                             {
                                 return false;
                             }
                         }
                         return true;
                     }()),
                "publish two effects with identical SDK paths"))
    {
        return 1;
    }
    auto* scene = world.create_scene("Effect lifecycle");
    auto camera_node = scene ? scene->create_node("Camera") : engine::Node{};
    auto camera = camera_node ? camera_node.add_component<Camera>() : Camera{};
    TransformSettings camera_transform;
    camera_transform.position = {0, 0, 15};
    if (!expect(scene && camera && scene->set_active_camera(camera) &&
                    camera_node.get_component<Transform>().set_settings(camera_transform),
                "create a camera-only scene") ||
        !expect(effects.draw(RenderContext{*scene}),
                "empty effect scene does not initialize or call an unavailable GPU") ||
        !expect(windows.initialize(config) && renderer.initialize(config), "initialize GPU") ||
        !advance(windows, renderer, effects))
    {
        return 1;
    }
    const auto idle_textures = bgfx::getStats()->numTextures;
    auto first_node = scene->create_node("First independent emitter");
    auto first = first_node ? first_node.add_component<EffectComponent>() : EffectComponent{};
    if (!expect(first && !first.play() && first.set_effect("red/effect"),
                "empty emitter rejects play and configured effect loads") ||
        !expect(!first.playing(), "play-on-awake is not reported before the SDK runs"))
    {
        return 1;
    }
    auto settings = first.settings();
    settings.loop = true;
    if (!expect(first.set_settings(settings), "loop the independent first emitter") ||
        !advance(windows, renderer, effects) || !expect(first.playing(), "play-on-awake starts"))
    {
        return 1;
    }
    // SDK renderer 自有默认 white/normal 纹理，单独量取初始化后无实例的基线。
    if (!expect(first.stop(), "retire the warm-up effect") || !advance(windows, renderer, effects))
    {
        return 1;
    }
    const auto renderer_textures = bgfx::getStats()->numTextures;
    if (!expect(first.play(), "restart after measuring renderer-only textures") ||
        !advance(windows, renderer, effects))
    {
        return 1;
    }
    const auto first_textures = bgfx::getStats()->numTextures;
    const auto binding_count = properties.GetColorImages().size();
    std::printf("Effect texture counts: idle=%u renderer=%u first=%u bindings=%zu\n",
                static_cast<unsigned>(idle_textures), static_cast<unsigned>(renderer_textures),
                static_cast<unsigned>(first_textures), binding_count);
    auto second_node = scene->create_node("Shared asset separate playback");
    auto second = second_node ? second_node.add_component<EffectComponent>() : EffectComponent{};
    if (!expect(first_textures == renderer_textures + binding_count && second &&
                    second.set_settings(settings) && second.instance_id() != first.instance_id(),
                "first instance owns textures and second has separate identity") ||
        !advance(windows, renderer, effects) ||
        !expect(first.playing() && second.playing() &&
                    bgfx::getStats()->numTextures == first_textures,
                "two instances share immutable effect and texture resources") ||
        !expect(first.pause(), "pause only the first emitter") ||
        !advance(windows, renderer, effects) ||
        !expect(!first.playing() && second.playing() && first.play(),
                "pause preserves the second instance and resumes explicitly") ||
        !advance(windows, renderer, effects))
    {
        return 1;
    }
    auto cold_node = scene->create_node("Deferred across world pause");
    auto cold = cold_node ? cold_node.add_component<EffectComponent>() : EffectComponent{};
    if (!expect(cold && first.pause() && world.set_paused(true) && cold.set_effect("red/effect") &&
                    effects.tick(120) && renderer.render() && renderer.render() &&
                    !cold.playing() && !first.playing() && second.playing(),
                "world pause blocks both tick and draw fallback emission, preserving individual "
                "pause") ||
        !expect(world.set_paused(false), "resume effect world") ||
        !advance(windows, renderer, effects) ||
        !expect(cold.playing() && !first.playing() && second.playing() && first.play() &&
                    scene->destroy_node(cold_node.id()),
                "world resume starts deferred emitter but not an individually paused instance") ||
        !advance(windows, renderer, effects))
    {
        return 1;
    }
    auto stopped_node = scene->create_node("Stopped across resource refresh");
    auto stopped = stopped_node ? stopped_node.add_component<EffectComponent>() : EffectComponent{};
    auto manual = second.settings();
    manual.play_on_awake = false;
    if (!expect(stopped && stopped.set_settings(settings) && stopped.stop() && first.pause() &&
                    second.set_settings(manual) &&
                    (fixture.publish(resources) && services.prepare_builtins() &&
                     [&]
                     {
                         for (const auto& entry : fixture.catalog.entries)
                         {
                             if (entry->path != "invalid/effect" && !services.load(entry->path))
                             {
                                 return false;
                             }
                         }
                         return true;
                     }()),
                "refresh catalog with paused, stopped and manually playing instances") ||
        !advance(windows, renderer, effects) ||
        !expect(!first.playing() && !stopped.playing() && second.playing(),
                "same-GUID generation refresh preserves each playback intent"))
    {
        return 1;
    }
    auto paused_data = first.data();
    auto stopped_data = stopped.data();
    auto playing_data = second.data();
    paused_data.payload.AsEffectData()->speed = 0.5F;
    stopped_data.payload.AsEffectData()->speed = 0.5F;
    playing_data.payload.AsEffectData()->speed = 0.5F;
    if (!expect(first.restore_data(paused_data) && stopped.restore_data(stopped_data) &&
                    second.restore_data(playing_data),
                "edit speed through scene/MCP restore contract") ||
        !advance(windows, renderer, effects) ||
        !expect(!first.playing() && !stopped.playing() && second.playing() && first.play() &&
                    second.set_settings(settings) && scene->destroy_node(stopped_node.id()),
                "same-resource edits preserve playback and pause remains resumable") ||
        !advance(windows, renderer, effects))
    {
        return 1;
    }
    auto invalid = settings;
    invalid.speed = std::numeric_limits<float>::quiet_NaN();
    if (!expect(!first.set_settings(invalid) && !first.set_effect("invalid/effect") &&
                    first.effect_path() == "red/effect" && first.playing(),
                "invalid settings or payload preserve the old resource and playback"))
    {
        return 1;
    }
    auto third_node = scene->create_node("Same SDK paths different bindings");
    auto third = third_node ? third_node.add_component<EffectComponent>() : EffectComponent{};
    if (!expect(third && third.set_effect("green/effect"), "load second effect variant"))
    {
        return 1;
    }
    auto green = third.settings();
    green.loop = true;
    if (!expect(third.set_settings(green), "loop the second effect resource") ||
        !advance(windows, renderer, effects) ||
        !expect(third.playing() && bgfx::getStats()->numTextures == first_textures + binding_count,
                "same authored texture paths resolve by each effect's GUID bindings"))
    {
        return 1;
    }
    const auto sprite_vertex_buffers = bgfx::getStats()->numVertexBuffers;
    const auto sprite_textures = bgfx::getStats()->numTextures;
    auto model_node = scene->create_node("Official model effect alongside sprite effects");
    auto model = model_node ? model_node.add_component<EffectComponent>() : EffectComponent{};
    if (!expect(model && model.set_effect("model/effect"), "load actual model effect"))
    {
        return 1;
    }
    auto model_settings = model.settings();
    model_settings.loop = true;
    if (!expect(model.set_settings(model_settings), "loop the model effect") ||
        !advance(windows, renderer, effects, 16) ||
        !expect(model.playing() && first.playing() && third.playing() &&
                    bgfx::getStats()->numVertexBuffers > sprite_vertex_buffers &&
                    bgfx::getStats()->numTextures ==
                        sprite_textures + model_properties.GetColorImages().size(),
                "model and sprite draw together; model renderer uploads real geometry") ||
        !expect(scene->destroy_node(model_node.id()), "retire the model effect node") ||
        !advance(windows, renderer, effects) ||
        !expect(bgfx::getStats()->numVertexBuffers == sprite_vertex_buffers &&
                    bgfx::getStats()->numTextures == sprite_textures,
                "model GPU geometry and textures retire while sprite instances keep playing") ||
        !expect(first.stop(), "stop first instance"))
    {
        return 1;
    }
    if (!second.set_enabled(false))
    {
        std::abort();
    }
    if (!advance(windows, renderer, effects) ||
        !expect(!first.playing() && !second.playing() && third.playing() &&
                    bgfx::getStats()->numTextures == sprite_textures,
                "disabled emitter retains its paused effect and shared resources"))
    {
        return 1;
    }
    if (!second.set_enabled(true))
    {
        std::abort();
    }
    if (!advance(windows, renderer, effects) ||
        !expect(second.playing(), "re-enable resumes the retained emitter"))
    {
        return 1;
    }
    const auto old_id = second.instance_id();
    if (!expect(second_node.remove_component(EffectComponent::component_type),
                "remove a playing effect component"))
    {
        return 1;
    }
    second = second_node.add_component<EffectComponent>();
    settings.play_on_awake = false;
    if (!expect(second && second.instance_id() != old_id && second.set_settings(settings),
                "replacement component has a new identity with explicit playback") ||
        !advance(windows, renderer, effects) ||
        !expect(!second.playing() && second.play(),
                "removed instance never revives in replacement") ||
        !advance(windows, renderer, effects) || !expect(second.playing(), "explicit play starts") ||
        !expect(scene->destroy_node(second_node.id()) && scene->destroy_node(third_node.id()) &&
                    scene->destroy_node(first_node.id()),
                "destroy all emitter nodes") ||
        !advance(windows, renderer, effects) ||
        !expect(bgfx::getStats()->numTextures == renderer_textures,
                "last emitter deletion retires all asset textures, retaining renderer defaults"))
    {
        return 1;
    }
    auto retained_node = scene->create_node("Retained emitter");
    auto retained =
        retained_node ? retained_node.add_component<EffectComponent>() : EffectComponent{};
    if (!expect(retained && retained_node.set_persistent(true) && retained.set_effect("red/effect"),
                "create persistent emitter"))
    {
        return 1;
    }
    auto retained_settings = retained.settings();
    retained_settings.loop = true;
    if (!expect(retained.set_settings(retained_settings) && retained.play(),
                "loop the retained effect") ||
        !advance(windows, renderer, effects) ||
        !expect(retained.playing(), "start retained effect"))
    {
        return 1;
    }
    const auto retained_id = retained.instance_id();
    world.unload_scene();
    if (!advance(windows, renderer, effects) ||
        !expect(world.scene() == nullptr && retained.instance_id() == retained_id &&
                    retained.playing() && bgfx::getStats()->numTextures >= binding_count + 2,
                "camera-less interval preserves live effect assets and SDK instance"))
    {
        return 1;
    }
    scene = world.create_scene("New effect scene");
    camera_node = scene ? scene->create_node("Camera") : engine::Node{};
    camera = camera_node ? camera_node.add_component<Camera>() : Camera{};
    if (!expect(camera && scene->set_active_camera(camera) &&
                    camera_node.get_component<Transform>().set_settings(camera_transform),
                "new scene camera views retained effects") ||
        !advance(windows, renderer, effects) ||
        !expect(retained.playing() && retained_node.scene()->destroy_node(retained_node.id()),
                "retained effect continues until explicitly destroyed") ||
        !advance(windows, renderer, effects) ||
        !expect(bgfx::getStats()->numTextures == renderer_textures,
                "retained emitter deletion releases its GPU textures"))
    {
        return 1;
    }
    effects.release_gpu();
    if (!advance(windows, renderer, effects) ||
        !expect(bgfx::getStats()->numTextures == idle_textures,
                "explicit GPU retirement also releases renderer default textures"))
    {
        return 1;
    }
    world.destroy_scene();
    renderer.release_scene();
    effects.release_scene();
    renderer.shutdown();
    effects.release_gpu();
    effects.shutdown();
    windows.shutdown();
    world.shutdown();
    services.shutdown();
    std::puts("Effect playback, GUID isolation, shared assets, removal and GPU retirement passed.");
    return 0;
}
