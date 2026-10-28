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

option(ENGINE_BUILD_APP "Build the example command-line application" ${PROJECT_IS_TOP_LEVEL})
option(ENGINE_BUILD_TESTS "Build this project's tests" ${PROJECT_IS_TOP_LEVEL})
option(ENGINE_WARNINGS_AS_ERRORS "Treat warnings in this project's targets as errors" OFF)
option(ENGINE_ENABLE_RUNTIME_DEPS "Discover planned runtime dependencies" OFF)
option(ENGINE_ENABLE_EDITOR_DEPS "Discover planned editor dependencies" OFF)
option(ENGINE_ENABLE_ASSET_PIPELINE_DEPS "Discover planned asset pipeline dependencies" OFF)

if(ENGINE_ENABLE_EDITOR_DEPS AND NOT ENGINE_ENABLE_RUNTIME_DEPS)
    message(FATAL_ERROR "Editor dependencies require ENGINE_ENABLE_RUNTIME_DEPS")
endif()

include(cmake/project_options.cmake)

add_subdirectory(third_party)

add_library(engine_core STATIC src/greeting.cpp)
add_library(engine::core ALIAS engine_core)
target_compile_features(engine_core PUBLIC cxx_std_20)
target_include_directories(engine_core PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_link_libraries(engine_core PRIVATE fmt::fmt)
engine_apply_options(engine_core)

if(ENGINE_BUILD_APP)
    add_executable(engine_app src/main.cpp)
    target_link_libraries(engine_app PRIVATE engine::core)
    engine_apply_options(engine_app)
endif()

if(PROJECT_IS_TOP_LEVEL OR ENGINE_BUILD_TESTS)
    include(CTest)
endif()

if(BUILD_TESTING AND ENGINE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
