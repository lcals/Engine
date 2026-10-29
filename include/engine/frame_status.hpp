#pragma once

namespace engine
{

enum class FrameStatus
{
    running,
    paused,
    exit_requested,
    failed
};

} // namespace engine
