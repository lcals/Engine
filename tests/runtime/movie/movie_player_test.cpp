#include "movie_fixture.hpp"
#include "resource_services.hpp"

#include "runtime/audio/audio_system.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/movie/movie_player.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/task/task_system.hpp"
#include "runtime/task/thread_system.hpp"

#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <thread>
#include <vector>

namespace
{
using namespace engine;
bool expect(bool value, const char* message)
{
    if (!value)
    {
        std::fprintf(stderr, "movie player: %s\n", message);
    }
    return value;
}

bool run()
{
    if (!SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy") || !SDL_Init(SDL_INIT_AUDIO))
    {
        return expect(false, "initialize SDL test audio");
    }
    ThreadSystem threads({2});
    TaskSystem tasks(threads, {"MovieTest", 2});
    test_support::ResourceServices resources(&threads);
    AudioSystem audio(&resources.loader);
    test_support::ResourceFixture fixture;
    if (!threads.initialize({}) || !tasks.initialize({}) || !resources.initialize({}) ||
        !audio.initialize({}) || !test_support::make_movie(fixture) ||
        !fixture.publish(resources.core))
    {
        return expect(false, "initialize services and fixture");
    }
    ResourceReference movie, sound;
    if (!resources.load("movies/test.mp4", movie))
    {
        return expect(false, "preload movie");
    }
    if (!resources.load("movies/sound.mp4", sound))
    {
        return expect(false, "preload sound movie");
    }
    MoviePlayer player(resources.loader, tasks, audio);
    MoviePlaybackHandle handle;
    if (!expect(player.play(movie.handle(), handle), "accept asynchronous movie") ||
        !expect(!player.play(movie.handle(), handle), "reject busy player"))
    {
        return false;
    }
    if (!player.current_handle(handle))
    {
        return false;
    }
    MoviePlaybackStatus status;
    MovieFrame frame;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    bool first_frame = false;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (!resources.tick())
        {
            return false;
        }
        (void)tasks.dispatch_completions(16, 0.01);
        if (!player.update(0) || !player.status(handle, status) ||
            status.state == MovieState::failed)
        {
            std::fprintf(stderr, "movie failure: %s\n", status.error.c_str());
            return false;
        }
        if (player.frame(handle, frame))
        {
            first_frame = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!expect(first_frame && frame.width == 8 && frame.height == 8 &&
                    frame.presentation_ticks == 0 && frame.planes[0].bytes.size() == 64,
                "first VP9 frame decoded asynchronously") ||
        !expect(player.set_paused(handle, true) && player.update(10) &&
                    player.status(handle, status) && status.position_seconds == 0,
                "paused clock stays still") ||
        !expect(player.set_paused(handle, false), "resume"))
    {
        return false;
    }
    const auto old_handle = handle;
    if (!expect(!player.stop(handle, static_cast<MovieStopReason>(99)) &&
                    player.status(handle, status) && status.state == MovieState::playing,
                "invalid stop reason leaves playback active") ||
        !expect(player.stop(handle, MovieStopReason::stopped) && player.release(handle) &&
                    player.play(movie.handle(), handle),
                "cancel an active decode and start a new request"))
    {
        return false;
    }
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (!resources.tick())
        {
            return false;
        }
        (void)tasks.dispatch_completions(16, 0.01);
        if (!player.update(0.02) || !player.status(handle, status) ||
            status.state == MovieState::failed)
        {
            return false;
        }
        if (status.state == MovieState::completed)
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!expect(status.state == MovieState::completed && player.frame(handle, frame) &&
                    frame.presentation_ticks == 2,
                "complete after final frame") ||
        !expect(player.release(handle), "release completed movie"))
    {
        return false;
    }
    MoviePlaybackHandle next;
    if (!expect(player.play(movie.handle(), next) &&
                    !player.stop(old_handle, MovieStopReason::stopped) &&
                    player.stop(next, MovieStopReason::skipped) && player.release(next),
                "old handle cannot affect subsequent request") ||
        !expect(player.play(sound.handle(), next), "accept movie with audio"))
    {
        return false;
    }
    bool saw_playing = false;
    bool paused_audio = false;
    const auto audio_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < audio_deadline)
    {
        if (!resources.tick())
        {
            return false;
        }
        (void)tasks.dispatch_completions(16, 0.01);
        if (!audio.tick(0.001) || !player.update(0.001) || !player.status(next, status) ||
            status.state == MovieState::failed)
        {
            std::fprintf(stderr, "audio movie failure: %s\n", status.error.c_str());
            return false;
        }
        saw_playing |= status.state == MovieState::playing;
        if (!paused_audio && status.state == MovieState::playing)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            if (!player.set_paused(next, true) || !player.status(next, status))
            {
                return false;
            }
            const auto paused_at = status.position_seconds;
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            if (!expect(player.update(1.0) && player.status(next, status) &&
                            status.state == MovieState::paused &&
                            status.position_seconds == paused_at,
                        "audio clock remains frozen while paused") ||
                !player.set_paused(next, false))
            {
                return false;
            }
            paused_audio = true;
        }
        if (status.state == MovieState::completed)
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return expect(saw_playing && paused_audio && status.state == MovieState::completed &&
                      status.position_seconds == status.duration_seconds &&
                      player.frame(next, frame) && frame.presentation_ticks == 2 &&
                      player.release(next),
                  "audio playback and video both finish");
}
bool catch_up()
{
    ThreadSystem threads({2});
    TaskSystem tasks(threads, {"MovieCatchUp", 2});
    test_support::ResourceServices resources(&threads);
    AudioSystem audio(&resources.loader);
    test_support::ResourceFixture fixture;
    if (!threads.initialize({}) || !tasks.initialize({}) || !resources.initialize({}) ||
        !audio.initialize({}) || !test_support::make_movie(fixture, 90, 30) ||
        !fixture.publish(resources.core))
    {
        return false;
    }
    ResourceReference movie;
    if (!resources.load("movies/test.mp4", movie))
    {
        return expect(false, "preload long movie");
    }
    MoviePlayer player(resources.loader, tasks, audio);
    MoviePlaybackHandle handle;
    MovieFrame frame;
    MoviePlaybackStatus status;
    if (!player.play(movie.handle(), handle))
    {
        return false;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!player.frame(handle, frame) && std::chrono::steady_clock::now() < deadline)
    {
        if (!resources.tick())
        {
            return false;
        }
        (void)tasks.dispatch_completions(16, 0.01);
        if (!player.update(0))
        {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!expect(player.frame(handle, frame), "catch-up fixture prepares first frame") ||
        !expect(player.update(1.5), "advance across a long host stall"))
    {
        return false;
    }
    // At most the previous prefetch and one catch-up job are needed, not 45 host updates.
    for (int pass = 0; pass < 3; ++pass)
    {
        if (!tasks.wait() || !player.update(0))
        {
            return false;
        }
    }
    if (!expect(player.frame(handle, frame) && frame.presentation_ticks == 45 &&
                    player.status(handle, status) && status.position_seconds == 1.5,
                "decode catches up to the current clock in bounded host updates"))
    {
        return false;
    }
    for (int pass = 0; pass < 5; ++pass)
    {
        if (!player.update(0.2) || !tasks.wait() || !player.update(0))
        {
            return false;
        }
        if (!expect(player.frame(handle, frame) && player.status(handle, status) &&
                        status.position_seconds -
                                static_cast<double>(frame.presentation_ticks) / 30 <
                            0.24,
                    "video does not accumulate lag at five host updates per second"))
        {
            return false;
        }
    }
    if (!player.update(10))
    {
        return false;
    }
    for (int pass = 0; pass < 3; ++pass)
    {
        if (!tasks.wait() || !player.update(0))
        {
            return false;
        }
    }
    return expect(player.status(handle, status) && status.state == MovieState::completed &&
                      player.frame(handle, frame) && frame.presentation_ticks == 89 &&
                      player.release(handle),
                  "catch-up reaches and retains the real final frame");
}

} // namespace
int main()
{
    return run() && catch_up() ? 0 : 1;
}
