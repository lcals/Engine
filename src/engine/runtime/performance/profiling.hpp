#pragma once

// Private implementation probes. Names must be static string literals for Tracy.
#if defined(ENGINE_ENABLE_TRACY)
#include <tracy/Tracy.hpp>

#define ENGINE_PROFILE_ZONE(name) ZoneScopedN(name)
#define ENGINE_PROFILE_FRAME() FrameMark
#define ENGINE_PROFILE_PLOT(name, value) TracyPlot(name, value)
#define ENGINE_PROFILE_MESSAGE(text, size) TracyMessage(text, size)
#else
#define ENGINE_PROFILE_ZONE(name) static_cast<void>(0)
#define ENGINE_PROFILE_FRAME() static_cast<void>(0)
#define ENGINE_PROFILE_PLOT(name, value) static_cast<void>(0)
#define ENGINE_PROFILE_MESSAGE(text, size) static_cast<void>(0)
#endif
