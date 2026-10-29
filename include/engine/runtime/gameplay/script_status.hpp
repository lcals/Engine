#pragma once

#include <string>

namespace engine
{

struct ScriptStatus
{
    bool running = false;
    bool hot_reload = false;
    std::string active_revision;
    std::string available_revision;
    std::string last_error;
};

} // namespace engine
