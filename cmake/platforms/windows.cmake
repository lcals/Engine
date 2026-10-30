if(NOT WIN32
   OR ANDROID
   OR NOT MSVC
   OR NOT ENGINE_BUILD_APP)
    return()
endif()
if(NOT TARGET engine_script
   OR NOT TARGET engine_resource_graph
   OR NOT TARGET asset_build_app)
    return()
endif()
if(NOT VCPKG_TARGET_TRIPLET STREQUAL "x64-windows-static"
   OR NOT CMAKE_MSVC_RUNTIME_LIBRARY
   OR CMAKE_MSVC_RUNTIME_LIBRARY MATCHES "DLL")
    return()
endif()

set(ENGINE_WINDOWS_PROJECT
    "${PROJECT_SOURCE_DIR}/projects/showcase"
    CACHE PATH "Windows project")
set(ENGINE_WINDOWS_MANIFEST
    "settings/resource-build.toml"
    CACHE STRING "Project build manifest")
set(ENGINE_WINDOWS_PRODUCT
    "engine-showcase"
    CACHE STRING "Windows application identity")
if(NOT ENGINE_WINDOWS_PRODUCT MATCHES "^[a-z][a-z0-9_-]*$")
    message(FATAL_ERROR "ENGINE_WINDOWS_PRODUCT must be a lowercase application identity")
endif()

# 正式项目直接运行此静态宿主；资源继续从 App 内置目录读取。
add_executable(engine_project_app EXCLUDE_FROM_ALL "${PROJECT_SOURCE_DIR}/src/main.cpp")
set_target_properties(engine_project_app PROPERTIES OUTPUT_NAME "${ENGINE_WINDOWS_PRODUCT}")
target_link_libraries(engine_project_app PRIVATE engine::runtime engine::app engine::log SDL3::SDL3
                                                 fmt::fmt)
target_compile_definitions(
    engine_project_app PRIVATE ENGINE_HAS_APP_SERVICES=1 ENGINE_START_PACKAGED_GAMEPLAY=1
                               ENGINE_PROJECT_APPLICATION_NAME="${ENGINE_WINDOWS_PRODUCT}")
engine_link_resource_plugin(engine_project_app)
engine_link_script_plugin(engine_project_app)
engine_link_character_plugin(engine_project_app)
engine_apply_options(engine_project_app)
engine_copy_script_data(engine_project_app)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_target(
    engine_project_export
    COMMAND
        "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/platforms/windows/export_project.py"
        --project "${ENGINE_WINDOWS_PROJECT}" --manifest "${ENGINE_WINDOWS_MANIFEST}" --builder
        "$<TARGET_FILE:asset_build_app>" --executable "$<TARGET_FILE:engine_project_app>" --icu
        "${ENGINE_V8_ICU_DATA}" --output "${PROJECT_BINARY_DIR}/output/${ENGINE_WINDOWS_PRODUCT}"
    DEPENDS engine_project_app asset_build_app
    COMMENT "Exporting static Windows executable and independent project resources"
    VERBATIM)
