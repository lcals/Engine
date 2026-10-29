#include "resource_fixture.hpp"

#include "runtime/audio/audio_system.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/scene/audio_listener.hpp"
#include "runtime/scene/audio_source.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/schema/audio_generated.h"
#include "runtime/world/world_system.hpp"

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_timer.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <numbers>
#include <string_view>

namespace
{

bool expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "audio test: %s\n", message);
    }
    return condition;
}

bool add_clip(test_support::ResourceFixture& fixture, std::string_view path, std::uint8_t channels)
{
    engine::schema::AudioClipDataT clip;
    clip.sample_rate = 48000;
    clip.channels = channels;
    clip.frames = 5760;
    for (std::uint64_t frame = 0; frame < clip.frames; ++frame)
    {
        const auto sample = static_cast<float>(
            std::sin(static_cast<double>(frame) * 440.0 * 2.0 * std::numbers::pi / 48000.0) * 0.2);
        for (std::uint8_t channel = 0; channel < channels; ++channel)
        {
            clip.samples.push_back(channel == 0 ? sample : -sample);
        }
    }
    flatbuffers::FlatBufferBuilder builder;
    engine::schema::FinishAudioClipDataBuffer(builder,
                                              engine::schema::AudioClipData::Pack(builder, &clip));
    auto entry = std::make_unique<engine::schema::AssetEntryT>();
    entry->guid = channels == 1 ? "20000000-0000-4000-8000-000000000001"
                                : "20000000-0000-4000-8000-000000000002";
    entry->path = path;
    entry->kind = engine::schema::AssetKind::audio;
    engine::ByteBuffer entity;
    const engine::ByteView payload(reinterpret_cast<const std::byte*>(builder.GetBufferPointer()),
                                   builder.GetSize());
    if (!engine::ResourceSystem::encode_resource(entry->guid, entry->kind, payload, entity,
                                                 entry->header_size))
    {
        return false;
    }
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto byte : entity)
    {
        hash = (hash ^ std::to_integer<std::uint8_t>(byte)) * 1099511628211ULL;
    }
    char digest[17]{};
    std::snprintf(digest, sizeof(digest), "%016llx", static_cast<unsigned long long>(hash));
    entry->content_hash = digest;
    entry->content_size = entity.size();
    entry->location = std::make_unique<engine::schema::FileLocationT>();
    entry->location->mount = "content";
    entry->location->path = "gen/" + entry->guid;
    entry->location->length = entity.size();
    std::ofstream file(fixture.root / entry->location->path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(entity.data()),
               static_cast<std::streamsize>(entity.size()));
    file.close();
    if (!file)
    {
        return false;
    }
    fixture.catalog.entries.push_back(std::move(entry));
    return true;
}

bool prepare(test_support::ResourceFixture& fixture, engine::ResourceSystem& resources)
{
    return !fixture.root.empty() && resources.initialize({}) &&
           add_clip(fixture, "project/audio/mono.wav", 1) &&
           add_clip(fixture, "project/audio/stereo.wav", 2) && fixture.publish(resources);
}

bool pump(engine::AudioSystem& audio, std::chrono::milliseconds duration)
{
    const auto end = std::chrono::steady_clock::now() + duration;
    do
    {
        if (!audio.tick(0.005))
        {
            std::fprintf(stderr, "audio tick: %.*s\n", static_cast<int>(audio.last_error().size()),
                         audio.last_error().data());
            return false;
        }
        SDL_Delay(5);
    } while (std::chrono::steady_clock::now() < end);
    return true;
}

bool wait_stopped(engine::AudioSystem& audio, const engine::AudioSource& source)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (source.playback() != engine::AudioPlayback::stopped)
    {
        if (std::chrono::steady_clock::now() > deadline ||
            !pump(audio, std::chrono::milliseconds(5)))
        {
            return false;
        }
    }
    return true;
}

bool settings_and_resources()
{
    test_support::ResourceFixture fixture;
    engine::ResourceSystem resources;
    engine::WorldSystem world;
    if (!expect(prepare(fixture, resources) && world.initialize({}), "prepare audio resources"))
    {
        return false;
    }
    auto* scene = world.create_scene("Audio settings");
    auto* node = scene ? scene->create_node("Speaker") : nullptr;
    auto* source = node ? node->add_component<engine::AudioSource>() : nullptr;
    auto* listener = node ? node->add_component<engine::AudioListener>() : nullptr;
    if (!expect(source && listener, "attach audio components") ||
        !expect(!source->play() && source->clip_data() == nullptr, "empty clip cannot play") ||
        !expect(source->set_clip("project/audio/mono.wav"), "load audio by virtual path"))
    {
        return false;
    }
    const auto* retained = source->clip_data();
    auto settings = source->settings();
    settings.clip_guid = "20000000-0000-4000-8000-000000000099";
    if (!expect(engine::AudioSource::valid_settings(settings) && !source->set_settings(settings) &&
                    source->clip_data() == retained,
                "pure settings validation does not require resource availability") ||
        !expect(!source->set_clip("project/audio/missing.wav") && source->clip_data() == retained,
                "failed clip replacement retains ownership"))
    {
        return false;
    }
    settings = source->settings();
    settings.pitch = 0.0F;
    if (!expect(!source->set_settings(settings) && source->settings().pitch == 1.0F,
                "invalid pitch leaves settings unchanged"))
    {
        return false;
    }
    settings = source->settings();
    settings.volume = std::numeric_limits<float>::quiet_NaN();
    if (!expect(!engine::AudioSource::valid_settings(settings), "reject nonfinite volume"))
    {
        return false;
    }
    settings = source->settings();
    settings.max_distance = settings.min_distance;
    auto listener_settings = listener->settings();
    listener_settings.volume = -1.0F;
    if (!expect(!source->set_settings(settings), "reject empty attenuation interval") ||
        !expect(!listener->set_settings(listener_settings) && listener->settings().volume == 1.0F,
                "listener rejects invalid gain"))
    {
        return false;
    }
    engine::schema::ComponentDataT snapshot;
    if (!expect(source->build_data(snapshot), "snapshot generated audio data") ||
        !expect(source->set_clip("") && source->clip_data() == nullptr &&
                    source->clip_path().empty(),
                "empty path clears clip") ||
        !expect(source->restore_data(snapshot) && source->clip_path() == "project/audio/mono.wav",
                "restore clip using its GUID") ||
        !expect(source->play() && source->playback() == engine::AudioPlayback::pending &&
                    !source->playing(),
                "accepted request is not reported as actual playback") ||
        !expect(source->pause() && source->playback() == engine::AudioPlayback::paused &&
                    source->play() && source->stop(),
                "request controls are explicit"))
    {
        return false;
    }
    engine::schema::ComponentDataT wrong;
    wrong.payload.Set(engine::schema::TransformDataT{});
    return expect(!source->restore_data(wrong) && !listener->restore_data(wrong),
                  "restore rejects a different component type");
}

bool playback_and_lifecycle()
{
    if (!SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "dummy", SDL_HINT_OVERRIDE))
    {
        return false;
    }
    test_support::ResourceFixture fixture;
    engine::ResourceSystem resources;
    engine::WorldSystem world;
    engine::AudioSystem audio(&world);
    if (!expect(prepare(fixture, resources) && world.initialize({}) && audio.initialize({}),
                "initialize without opening an audio device") ||
        !expect(audio.tick(0.0) && audio.voice_count() == 0 && SDL_WasInit(SDL_INIT_AUDIO) == 0,
                "empty world leaves the audio subsystem unopened"))
    {
        return false;
    }
    auto* scene = world.create_scene("Audio playback");
    auto* node = scene ? scene->create_node("One shot") : nullptr;
    auto* source = node ? node->add_component<engine::AudioSource>() : nullptr;
    if (!expect(source && source->set_clip("project/audio/mono.wav"), "prepare one-shot source") ||
        !expect(audio.tick(0.0) && source->playing() && audio.voice_count() == 1,
                "play-on-awake creates a real SDL stream even when spatially muted") ||
        !expect(SDL_GetCurrentAudioDriver() &&
                    std::string_view(SDL_GetCurrentAudioDriver()) == "dummy",
                "test explicitly uses dummy output") ||
        !expect(wait_stopped(audio, *source) && audio.voice_count() == 0,
                "SDL consumes a one-shot stream and the voice is released") ||
        !expect(audio.tick(0.0) && !source->playing() && audio.voice_count() == 0,
                "play-on-awake does not repeat after completion"))
    {
        return false;
    }
    auto* listener_node = scene->create_node("Listener");
    auto* listener =
        listener_node ? listener_node->add_component<engine::AudioListener>() : nullptr;
    auto settings = source->settings();
    settings.loop = true;
    settings.pitch = 1.5F;
    settings.volume = 0.5F;
    if (!expect(listener && source->set_settings(settings) && source->play() && audio.tick(0.0),
                "explicit looping source with listener") ||
        !expect(pump(audio, std::chrono::milliseconds(350)) && source->playing() &&
                    audio.voice_count() == 1,
                "loop survives several clip durations"))
    {
        return false;
    }
    engine::schema::ComponentDataT edited;
    if (!expect(source->build_data(edited), "capture source for component editing"))
    {
        return false;
    }
    auto* edited_settings = edited.payload.AsAudioSourceData();
    edited_settings->volume = 0.25F;
    edited_settings->pitch = 0.75F;
    if (!expect(source->restore_data(edited) && source->playing() && audio.tick(0.0) &&
                    source->playing() && audio.voice_count() == 1 &&
                    source->settings().volume == 0.25F && source->settings().pitch == 0.75F,
                "editing same-clip settings preserves active playback") ||
        !expect(source->pause() && audio.tick(0.0) && !source->playing() &&
                    audio.voice_count() == 1,
                "source pause retains one unbound stream"))
    {
        return false;
    }
    edited_settings->volume = 0.5F;
    if (!expect(source->restore_data(edited) &&
                    source->playback() == engine::AudioPlayback::paused && audio.tick(0.0) &&
                    source->playback() == engine::AudioPlayback::paused && audio.voice_count() == 1,
                "editing same-clip settings preserves paused playback") ||
        !expect(pump(audio, std::chrono::milliseconds(150)) && source->play() && audio.tick(0.0) &&
                    source->playing(),
                "paused source resumes without creating another voice"))
    {
        return false;
    }
    auto* second_node = scene->create_node("Stereo");
    auto* second = second_node ? second_node->add_component<engine::AudioSource>() : nullptr;
    if (!expect(second && second->set_clip("project/audio/stereo.wav"), "load stereo clip"))
    {
        return false;
    }
    auto second_settings = second->settings();
    second_settings.spatial = false;
    if (!expect(second->set_settings(second_settings) && audio.set_paused(true) &&
                    audio.tick(0.0) && audio.paused() && audio.voice_count() == 2 &&
                    second->playing(),
                "background pause applies to all active streams") ||
        !expect(pump(audio, std::chrono::milliseconds(250)) && second->playing(),
                "paused device does not consume the non-looping stream") ||
        !expect(audio.set_paused(false) && wait_stopped(audio, *second) && source->playing() &&
                    audio.voice_count() == 1,
                "device resume drains only the one-shot source"))
    {
        return false;
    }
    auto transform = listener_node->get_component<engine::Transform>()->settings();
    transform.position = engine::Vec3(100.0, 0.0, 0.0);
    transform.rotation = engine::Vec3(0.0, 90.0, 0.0);
    if (!expect(listener_node->get_component<engine::Transform>()->set_settings(transform) &&
                    audio.tick(0.0) && source->playing(),
                "distance and listener orientation update live"))
    {
        return false;
    }
    source->set_enabled(false);
    if (!expect(audio.tick(0.0) && audio.voice_count() == 0 &&
                    source->playback() == engine::AudioPlayback::stopped && !source->play(),
                "disabled source stops and cannot request output"))
    {
        return false;
    }
    source->set_enabled(true);
    if (!expect(audio.tick(0.0) && audio.voice_count() == 0,
                "reenabling does not retrigger play-on-awake") ||
        !expect(source->play() && audio.tick(0.0) && scene->destroy_node(node->id()) &&
                    audio.tick(0.0) && audio.voice_count() == 0,
                "deleted source leaves no stream or dangling component"))
    {
        return false;
    }
    if (!expect(second->play() && audio.tick(0.0) && audio.voice_count() == 1,
                "start a voice before scene destruction"))
    {
        return false;
    }
    world.destroy_scene();
    if (!expect(audio.tick(0.0) && audio.voice_count() == 0 && SDL_WasInit(SDL_INIT_AUDIO) == 0,
                "scene destruction closes all streams and the logical device"))
    {
        return false;
    }
    scene = world.create_scene("New audio scene");
    node = scene ? scene->create_node("New source") : nullptr;
    source = node ? node->add_component<engine::AudioSource>() : nullptr;
    if (!expect(source && source->set_clip("project/audio/mono.wav") && source->play(),
                "new scene has independent source identity"))
    {
        return false;
    }
    audio.clear();
    audio.clear();
    if (!expect(audio.tick(0.0) && audio.voice_count() == 0 &&
                    source->playback() == engine::AudioPlayback::stopped,
                "clear cancels even pending requests without restarting awake playback") ||
        !expect(source->play() && audio.tick(0.0) && source->playing(),
                "explicit playback works after clear"))
    {
        return false;
    }
    audio.shutdown();
    audio.shutdown();
    return expect(audio.voice_count() == 0 && SDL_WasInit(SDL_INIT_AUDIO) == 0 &&
                      source->playback() == engine::AudioPlayback::stopped,
                  "shutdown is repeatable and releases the owned SDL subsystem");
}

bool unavailable_device()
{
    if (!SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "engine-missing-audio-driver",
                                 SDL_HINT_OVERRIDE))
    {
        return false;
    }
    test_support::ResourceFixture fixture;
    engine::ResourceSystem resources;
    engine::WorldSystem world;
    engine::AudioSystem audio(&world);
    if (!expect(prepare(fixture, resources) && world.initialize({}) && audio.initialize({}) &&
                    audio.tick(0),
                "absence of audio hardware does not prevent idle engine initialization"))
    {
        return false;
    }
    auto* scene = world.create_scene("Unavailable output");
    auto* node = scene ? scene->create_node("Source") : nullptr;
    auto* source = node ? node->add_component<engine::AudioSource>() : nullptr;
    if (!expect(source && source->set_clip("project/audio/mono.wav") && source->play(),
                "playback request is accepted before opening the device") ||
        !expect(!audio.tick(0.0) && source->playback() == engine::AudioPlayback::failed &&
                    !source->playing() && audio.voice_count() == 0 && !audio.last_error().empty(),
                "unavailable device reports failure instead of fake playback"))
    {
        return false;
    }
    if (!SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "dummy", SDL_HINT_OVERRIDE))
    {
        return false;
    }
    return expect(source->play() && audio.tick(0.0) && source->playing() &&
                      audio.last_error().empty(),
                  "an explicit retry can recover after output becomes available");
}

} // namespace

int main()
{
    return settings_and_resources() && playback_and_lifecycle() && unavailable_device() ? 0 : 1;
}
