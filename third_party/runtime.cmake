# Windows、macOS 和 Linux 游戏运行时依赖。
find_package(SDL3 CONFIG REQUIRED GLOBAL)
find_package(bgfx CONFIG REQUIRED GLOBAL)
find_package(flecs CONFIG REQUIRED GLOBAL)
find_package(Jolt CONFIG REQUIRED GLOBAL)
find_package(recastnavigation CONFIG REQUIRED GLOBAL)
find_package(RmlUi CONFIG REQUIRED GLOBAL)
find_package(flatbuffers CONFIG REQUIRED GLOBAL)
find_package(tomlplusplus CONFIG REQUIRED GLOBAL)
find_package(basisu CONFIG REQUIRED GLOBAL)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ozz-animation/CMakeLists.txt")
    message(FATAL_ERROR "ozz-animation is missing; run git submodule update --init")
endif()

find_package(Git REQUIRED)
set(_ozz_source "${CMAKE_CURRENT_SOURCE_DIR}/ozz-animation")
set(_ozz_staged "${CMAKE_CURRENT_BINARY_DIR}/source/ozz-animation")
set(_ozz_overrides "${CMAKE_CURRENT_SOURCE_DIR}/overrides/ozz-animation")
set(_ozz_prepare_script "${PROJECT_SOURCE_DIR}/cmake/prepare_dependency.cmake")
execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${_ozz_source}" rev-parse --git-path HEAD
    OUTPUT_VARIABLE _ozz_git_head
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _ozz_git_head_result)
if(NOT _ozz_git_head_result EQUAL 0)
    message(FATAL_ERROR "Cannot locate ozz-animation Git HEAD")
endif()

# 覆盖文件和子模块版本变化时，先重新配置；构建目标每次执行覆盖。
file(GLOB_RECURSE _ozz_override_files CONFIGURE_DEPENDS LIST_DIRECTORIES FALSE
     "${_ozz_overrides}/*")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${_ozz_prepare_script}" "${_ozz_git_head}" ${_ozz_override_files})

set(_ozz_prepare_arguments
    "-DENGINE_DEP_NAME=ozz-animation"
    "-DENGINE_DEP_SOURCE=${_ozz_source}"
    "-DENGINE_DEP_STAGED=${_ozz_staged}"
    "-DENGINE_DEP_OVERRIDES=${_ozz_overrides}"
    "-DENGINE_DEP_BUILD_ROOT=${CMAKE_BINARY_DIR}"
    "-DENGINE_DEP_GIT_EXECUTABLE=${GIT_EXECUTABLE}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" ${_ozz_prepare_arguments}
            "-DENGINE_DEP_MODE=CONFIGURE" -P "${_ozz_prepare_script}"
    RESULT_VARIABLE _ozz_prepare_result)
if(NOT _ozz_prepare_result EQUAL 0)
    message(FATAL_ERROR "Failed to prepare ozz-animation")
endif()

set(ozz_build_tools OFF CACHE BOOL "Build ozz tools" FORCE)
set(ozz_build_samples OFF CACHE BOOL "Build ozz samples" FORCE)
set(ozz_build_howtos OFF CACHE BOOL "Build ozz howtos" FORCE)
set(ozz_build_tests OFF CACHE BOOL "Build ozz tests" FORCE)
add_subdirectory("${_ozz_staged}" "${CMAKE_CURRENT_BINARY_DIR}/ozz-animation" EXCLUDE_FROM_ALL)

add_custom_target(engine_prepare_ozz_animation ALL
    COMMAND "${CMAKE_COMMAND}" ${_ozz_prepare_arguments}
            "-DENGINE_DEP_MODE=BUILD" -P "${_ozz_prepare_script}"
    VERBATIM)

# 所有 ozz 目标（含源码融合目标）都必须在编译前完成覆盖。
function(_engine_ozz_add_prepare_dependency _directory)
    get_property(_targets DIRECTORY "${_directory}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_target IN LISTS _targets)
        add_dependencies("${_target}" engine_prepare_ozz_animation)
    endforeach()
    get_property(_subdirectories DIRECTORY "${_directory}" PROPERTY SUBDIRECTORIES)
    foreach(_subdirectory IN LISTS _subdirectories)
        _engine_ozz_add_prepare_dependency("${_subdirectory}")
    endforeach()
endfunction()
_engine_ozz_add_prepare_dependency("${_ozz_staged}")

# V8 使用 GN；当前只固定脚本运行时所需源码，尚未接入构建。
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/v8/BUILD.gn")
    message(FATAL_ERROR "V8 is missing; run git submodule update --init")
endif()
