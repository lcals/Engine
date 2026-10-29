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
option(ENGINE_ENABLE_SCRIPT_RUNTIME "Build the desktop gameplay scripting and synchronization runtime" OFF)

if(ENGINE_ENABLE_SCRIPT_RUNTIME AND
   (NOT ENGINE_ENABLE_V8 OR NOT ENGINE_ENABLE_RUNTIME_DEPS OR ANDROID))
    message(FATAL_ERROR "Script runtime currently requires desktop runtime dependencies and V8")
endif()

if(ENGINE_ENABLE_EDITOR_DEPS AND NOT ENGINE_ENABLE_RUNTIME_DEPS)
    message(FATAL_ERROR "Editor dependencies require ENGINE_ENABLE_RUNTIME_DEPS")
endif()

if(ENGINE_ENABLE_SCRIPT_RUNTIME AND ENGINE_ENABLE_EDITOR_DEPS AND
   NOT ENGINE_ENABLE_ASSET_PIPELINE_DEPS)
    message(FATAL_ERROR "Editor scripting requires ENGINE_ENABLE_ASSET_PIPELINE_DEPS")
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
    if(ANDROID)
        add_library(engine_app SHARED src/main.cpp)
        set_target_properties(engine_app PROPERTIES OUTPUT_NAME main)
    else()
        add_executable(engine_app src/main.cpp)
    endif()
    target_link_libraries(engine_app PRIVATE engine::runtime SDL3::SDL3 fmt::fmt)
    if(TARGET engine_platform)
        target_link_libraries(engine_app PRIVATE engine::app engine::log)
        target_compile_definitions(engine_app PRIVATE ENGINE_HAS_DESKTOP_SERVICES=1)
    else()
        target_compile_definitions(engine_app PRIVATE ENGINE_HAS_DESKTOP_SERVICES=0)
    endif()
    engine_apply_options(engine_app)
    if(ANDROID)
        include(platforms/android/native_bundle.cmake)
    endif()
elseif(ENGINE_BUILD_APP)
    message(STATUS "engine_app requires ENGINE_ENABLE_RUNTIME_DEPS; building core only")
endif()

if(ENGINE_BUILD_APP AND ENGINE_ENABLE_EDITOR_DEPS)
    add_executable(editor_app src/editor_main.cpp)
    target_link_libraries(editor_app PRIVATE engine::editor engine::project engine::runtime
        engine::app engine::log SDL3::SDL3 fmt::fmt)
    if(ENGINE_ENABLE_SCRIPT_RUNTIME)
        if(NOT ENGINE_ENABLE_ASSET_PIPELINE_DEPS)
            message(FATAL_ERROR "Editor scripting requires ENGINE_ENABLE_ASSET_PIPELINE_DEPS")
        endif()
        target_link_libraries(editor_app PRIVATE engine::script engine::asset_pipeline
            engine::asset_sync)
    endif()
    engine_apply_options(editor_app)
endif()

if(ENGINE_BUILD_APP AND ENGINE_ENABLE_SCRIPT_RUNTIME AND ENGINE_ENABLE_ASSET_PIPELINE_DEPS)
    add_executable(mcp_app src/mcp_main.cpp)
    target_link_libraries(mcp_app PRIVATE engine::mcp engine::asset_pipeline engine::asset_sync
        SDL3::SDL3)
    engine_apply_options(mcp_app)
endif()

function(engine_copy_script_data target)
    if(ENGINE_ENABLE_SCRIPT_RUNTIME)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${ENGINE_V8_ICU_DATA}" "$<TARGET_FILE_DIR:${target}>/icudtl.dat"
            VERBATIM)
    endif()
endfunction()
foreach(_engine_host engine_app editor_app)
    if(TARGET ${_engine_host})
        engine_copy_script_data(${_engine_host})
    endif()
endforeach()

if(PROJECT_IS_TOP_LEVEL OR ENGINE_BUILD_TESTS)
    include(CTest)
endif()

if(BUILD_TESTING AND ENGINE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
