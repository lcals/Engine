#pragma once

#include "runtime/movie/movie_frame.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace engine
{
class ResourceSystem;
class TaskSystem;
class AudioSystem;

struct MoviePlaybackHandle
{
    std::uint64_t id = 0;
    std::uint64_t generation = 0;
};

enum class MovieState
{
    preparing,
    playing,
    paused,
    completed,
    cancelled,
    failed
};

enum class MovieStopReason
{
    none,
    stopped,
    skipped
};

struct MoviePlaybackStatus
{
    MovieState state = MovieState::preparing;
    MovieStopReason stop_reason = MovieStopReason::none;
    double position_seconds = 0;
    double duration_seconds = 0;
    std::string error;
};

class MoviePlayer final
{
public:
    MoviePlayer(ResourceSystem& resources, TaskSystem& tasks, AudioSystem& audio) noexcept;
    ~MoviePlayer() noexcept;
    MoviePlayer(const MoviePlayer&) = delete;
    MoviePlayer& operator=(const MoviePlayer&) = delete;
    MoviePlayer(MoviePlayer&&) = delete;
    MoviePlayer& operator=(MoviePlayer&&) = delete;

    [[nodiscard]] bool play(std::string_view path, MoviePlaybackHandle& output);
    [[nodiscard]] bool set_paused(MoviePlaybackHandle handle, bool paused);
    [[nodiscard]] bool stop(MoviePlaybackHandle handle, MovieStopReason reason);
    [[nodiscard]] bool status(MoviePlaybackHandle handle, MoviePlaybackStatus& output) const;
    [[nodiscard]] bool current_handle(MoviePlaybackHandle& output) const noexcept;
    [[nodiscard]] bool release(MoviePlaybackHandle handle);
    [[nodiscard]] bool update(double delta_time);
    [[nodiscard]] bool frame(MoviePlaybackHandle handle, MovieFrame& output) const;
    [[nodiscard]] std::string_view last_error() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace engine
