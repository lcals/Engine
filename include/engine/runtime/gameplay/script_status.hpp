#pragma once

#include <cstdint>
#include <string>

namespace engine
{

struct ScriptStatus
{
    bool running = false;
    bool hot_reload = false;
    std::uint64_t run_id = 0;
    std::string active_revision;
    std::string available_revision;
    std::string last_error;
};

} // namespace engine
