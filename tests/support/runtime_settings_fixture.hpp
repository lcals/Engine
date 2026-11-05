#pragma once

#include "runtime/app/runtime_settings.hpp"

#include <cstdio>
#include <string>

namespace test_support
{

[[nodiscard]] inline bool load_runtime_settings(engine::RuntimeSettings& output)
{
    std::string error;
    if (!engine::load_runtime_settings(ENGINE_TEST_RUNTIME_SETTINGS, output, error))
    {
        std::fprintf(stderr, "runtime settings fixture: %s\n", error.c_str());
        return false;
    }
    return true;
}

} // namespace test_support
