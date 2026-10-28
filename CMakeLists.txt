cmake_minimum_required(VERSION 3.25)

project(engine VERSION 0.1.0 DESCRIPTION "Engine C++20 project" LANGUAGES CXX)

# 本地 vcpkg port 文件变化时，下次构建先重新配置并更新依赖。
file(GLOB_RECURSE ENGINE_VCPKG_OVERLAY_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/overlay-ports/*")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg-configuration.json"
    ${ENGINE_VCPKG_OVERLAY_FILES})

if(PROJECT_IS_TOP_LEVEL AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build configuration for single-config generators" FORCE)
endif()

option(ENGINE_BUILD_APP "Build the minimal window application" ${PROJECT_IS_TOP_LEVEL})
option(ENGINE_BUILD_TESTS "Build this project's tests" ${PROJECT_IS_TOP_LEVEL})
option(ENGINE_WARNINGS_AS_ERRORS "Treat warnings in this project's targets as errors" OFF)
option(ENGINE_ENABLE_RUNTIME_DEPS "Build runtime systems and dependencies" OFF)
option(ENGINE_ENABLE_EDITOR_DEPS "Build editor system and dependencies" OFF)
option(ENGINE_ENABLE_ASSET_PIPELINE_DEPS "Build asset pipeline system and dependencies" OFF)

if(ENGINE_ENABLE_EDITOR_DEPS AND NOT ENGINE_ENABLE_RUNTIME_DEPS)
    message(FATAL_ERROR "Editor dependencies require ENGINE_ENABLE_RUNTIME_DEPS")
endif()

option(ENGINE_ENABLE_GPU_TESTS "Register tests requiring a desktop and real GPU" OFF)

include(cmake/compiler_cache.cmake)
include(cmake/project_options.cmake)

add_subdirectory(third_party)

add_library(engine_core STATIC src/engine/system/system.cpp)
add_library(engine::core ALIAS engine_core)
target_compile_features(engine_core PUBLIC cxx_std_20)
target_include_directories(engine_core PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/engine>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/engine/system>")
engine_apply_options(engine_core)

include(cmake/engine_modules.cmake)

if(ENGINE_BUILD_APP AND ENGINE_ENABLE_RUNTIME_DEPS)
    add_executable(engine_app src/main.cpp)
    target_link_libraries(engine_app PRIVATE engine::runtime SDL3::SDL3 fmt::fmt)
    engine_apply_options(engine_app)
elseif(ENGINE_BUILD_APP)
    message(STATUS "engine_app requires ENGINE_ENABLE_RUNTIME_DEPS; building core only")
endif()

if(PROJECT_IS_TOP_LEVEL OR ENGINE_BUILD_TESTS)
    include(CTest)
endif()

if(BUILD_TESTING AND ENGINE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
