# 本地 vcpkg port 文件变化时，下次构建先重新配置并更新依赖。
file(GLOB_RECURSE ENGINE_VCPKG_OVERLAY_FILES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/third_party/overlay-ports/*")
set_property(
    DIRECTORY
    APPEND
    PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg-configuration.json"
             ${ENGINE_VCPKG_OVERLAY_FILES})

if(PROJECT_IS_TOP_LEVEL
   AND NOT CMAKE_BUILD_TYPE
   AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE
        Debug
        CACHE STRING "Build configuration for single-config generators" FORCE)
endif()

option(ENGINE_BUILD_APP "Build the minimal window application" ${PROJECT_IS_TOP_LEVEL})
option(ENGINE_BUILD_TESTS "Build this project's tests" ${PROJECT_IS_TOP_LEVEL})
option(ENGINE_WARNINGS_AS_ERRORS "Treat warnings in this project's targets as errors" OFF)
option(ENGINE_ENABLE_RUNTIME_DEPS "Build runtime systems and dependencies" OFF)
option(ENGINE_ENABLE_EDITOR_DEPS "Build editor system and dependencies" OFF)
option(ENGINE_ENABLE_ASSET_PIPELINE_DEPS "Build asset pipeline system and dependencies" OFF)
option(ENGINE_ENABLE_SCRIPT_RUNTIME "Build the gameplay scripting runtime" OFF)

if("runtime/script" IN_LIST ENGINE_SELECTED_PLUGINS AND NOT ENGINE_ENABLE_RUNTIME_DEPS)
    message(FATAL_ERROR "Script runtime requires runtime dependencies")
endif()

if(ENGINE_ENABLE_EDITOR_DEPS AND NOT ENGINE_ENABLE_RUNTIME_DEPS)
    message(FATAL_ERROR "Editor dependencies require ENGINE_ENABLE_RUNTIME_DEPS")
endif()

if(ENGINE_ENABLE_ASSET_PIPELINE_DEPS AND NOT ENGINE_ENABLE_RUNTIME_DEPS)
    message(FATAL_ERROR "Asset pipeline requires ENGINE_ENABLE_RUNTIME_DEPS")
endif()

if("runtime/script" IN_LIST ENGINE_SELECTED_PLUGINS
   AND ENGINE_ENABLE_EDITOR_DEPS
   AND NOT ENGINE_ENABLE_ASSET_PIPELINE_DEPS)
    message(FATAL_ERROR "Editor scripting requires ENGINE_ENABLE_ASSET_PIPELINE_DEPS")
endif()

option(ENGINE_ENABLE_GPU_TESTS "Register tests requiring a desktop and real GPU" OFF)
