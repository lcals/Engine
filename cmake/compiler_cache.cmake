include_guard(GLOBAL)

option(ENGINE_USE_COMPILER_CACHE "Use ccache or sccache for CMake compilation" ON)
option(ENGINE_REQUIRE_COMPILER_CACHE "Fail configuration when compiler caching is unavailable" OFF)

if(ENGINE_REQUIRE_COMPILER_CACHE AND NOT ENGINE_USE_COMPILER_CACHE)
    message(FATAL_ERROR "ENGINE_REQUIRE_COMPILER_CACHE requires ENGINE_USE_COMPILER_CACHE=ON")
endif()

if(ENGINE_USE_COMPILER_CACHE AND CMAKE_GENERATOR MATCHES "Makefiles|Ninja")
    if(CMAKE_HOST_WIN32)
        set(_engine_cache_names sccache ccache)
    else()
        set(_engine_cache_names ccache sccache)
    endif()
    # 编译缓存运行在宿主机；交叉编译时不能在 Android / iOS sysroot 中查找。
    find_program(
        ENGINE_COMPILER_CACHE_PROGRAM
        NAMES ${_engine_cache_names}
        NO_CMAKE_FIND_ROOT_PATH
        DOC "Compiler cache launcher used by engine and ozz targets")
    if(ENGINE_COMPILER_CACHE_PROGRAM)
        execute_process(
            COMMAND "${ENGINE_COMPILER_CACHE_PROGRAM}" --version
            RESULT_VARIABLE _engine_cache_result
            OUTPUT_QUIET ERROR_QUIET
            TIMEOUT 10)
        if(NOT _engine_cache_result EQUAL 0)
            message(
                FATAL_ERROR
                    "Compiler cache cannot run: ${ENGINE_COMPILER_CACHE_PROGRAM}. "
                    "Run scripts/setup.sh --install or scripts/setup.ps1 -Install, "
                    "then reconfigure with a working ENGINE_COMPILER_CACHE_PROGRAM.")
        endif()
        message(STATUS "Compiler cache: ${ENGINE_COMPILER_CACHE_PROGRAM}")
    elseif(ENGINE_REQUIRE_COMPILER_CACHE)
        message(
            FATAL_ERROR
                "Compiler cache is required. Run scripts/setup.sh --install "
                "or scripts/setup.ps1 -Install to install ccache / sccache, then reconfigure.")
    else()
        message(STATUS "Compiler cache unavailable; install ccache or sccache to enable reuse")
    endif()
elseif(ENGINE_REQUIRE_COMPILER_CACHE)
    message(FATAL_ERROR "Compiler cache requires a Makefiles or Ninja generator. "
                        "Use the ninja-debug / ninja-release presets.")
elseif(ENGINE_USE_COMPILER_CACHE)
    message(STATUS "Compiler cache launchers require a Makefiles or Ninja generator")
endif()

function(engine_apply_compiler_cache target)
    if(NOT ENGINE_USE_COMPILER_CACHE
       OR NOT ENGINE_COMPILER_CACHE_PROGRAM
       OR NOT CMAKE_GENERATOR MATCHES "Makefiles|Ninja")
        return()
    endif()
    foreach(_language IN ITEMS C CXX OBJC OBJCXX)
        get_target_property(_existing_launcher ${target} ${_language}_COMPILER_LAUNCHER)
        # 尊重宿主通过 CMAKE_<LANG>_COMPILER_LAUNCHER 设置的启动器。
        if(NOT _existing_launcher)
            set_property(TARGET ${target} PROPERTY ${_language}_COMPILER_LAUNCHER
                                                   "${ENGINE_COMPILER_CACHE_PROGRAM}")
        endif()
    endforeach()
    if(MSVC)
        # /Zi 的共享编译 PDB 无法可靠缓存；/Z7 把调试信息放入独立对象文件。
        set_property(TARGET ${target} PROPERTY MSVC_DEBUG_INFORMATION_FORMAT
                                               "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")
    endif()
endfunction()
