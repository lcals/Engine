if(ANDROID
   OR IOS
   OR NOT ENGINE_BUILD_APP
   OR NOT (WIN32 OR APPLE))
    return()
endif()
if(NOT TARGET engine_script
   OR NOT TARGET engine_resource_graph
   OR NOT TARGET asset_build_app)
    return()
endif()
if(WIN32
   AND (NOT VCPKG_TARGET_TRIPLET STREQUAL "x64-windows-static"
        OR NOT CMAKE_MSVC_RUNTIME_LIBRARY
        OR CMAKE_MSVC_RUNTIME_LIBRARY MATCHES "DLL"))
    return()
endif()

set(ENGINE_EXPORT_PROJECT
    "${PROJECT_SOURCE_DIR}/projects/showcase"
    CACHE PATH "Exported project")
set(ENGINE_EXPORT_MANIFEST
    "settings/resource-build.toml"
    CACHE STRING "Project build manifest")
set(ENGINE_EXPORT_PRODUCT
    "engine-showcase"
    CACHE STRING "Project application identity")
if(NOT ENGINE_EXPORT_PRODUCT MATCHES "^[a-z][a-z0-9_-]*$")
    message(FATAL_ERROR "ENGINE_EXPORT_PRODUCT must be a lowercase application identity")
endif()

# 项目宿主只装配运行时插件，资源从 App 的内置目录独立读取。
add_executable(engine_project_app EXCLUDE_FROM_ALL "${PROJECT_SOURCE_DIR}/src/main.cpp")
set_target_properties(engine_project_app PROPERTIES OUTPUT_NAME "${ENGINE_EXPORT_PRODUCT}")
target_link_libraries(engine_project_app PRIVATE engine::runtime engine::app engine::log SDL3::SDL3
                                                 fmt::fmt)
target_compile_definitions(
    engine_project_app PRIVATE ENGINE_HAS_APP_SERVICES=1 ENGINE_START_PACKAGED_GAMEPLAY=1
                               ENGINE_PROJECT_APPLICATION_NAME="${ENGINE_EXPORT_PRODUCT}")
engine_link_resource_plugin(engine_project_app)
engine_link_script_plugin(engine_project_app)
engine_link_character_plugin(engine_project_app)
engine_apply_options(engine_project_app)
engine_copy_script_data(engine_project_app)
