include_guard(GLOBAL)

# Select the deployment target before creating any engine or dependency targets.
# iOS and other Apple platforms keep their own platform/toolchain requirements.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    return()
endif()

set(_engine_macos_minimum "13.3")
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET
        "${_engine_macos_minimum}"
        CACHE STRING "Minimum supported macOS version" FORCE)
elseif(CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS _engine_macos_minimum)
    message(FATAL_ERROR "Engine requires macOS ${_engine_macos_minimum} or newer; "
                        "CMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}")
endif()
