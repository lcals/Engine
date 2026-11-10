cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED ENGINE_SOURCE_DIR)
    get_filename_component(ENGINE_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
if(NOT DEFINED ENGINE_OUTPUT_DIR)
    set(ENGINE_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/plugin-selection-test")
endif()
file(MAKE_DIRECTORY "${ENGINE_OUTPUT_DIR}")

# 独立进程隔离 GLOBAL properties 和 CACHE；直接执行真实清单解析而不触发依赖安装。
function(run_case name expected_error body)
    set(_script "${ENGINE_OUTPUT_DIR}/${name}.cmake")
    file(WRITE "${_script}" "cmake_minimum_required(VERSION 3.25)\n")
    file(APPEND "${_script}" "set(ROOT [==[${ENGINE_SOURCE_DIR}]==])\n")
    file(APPEND "${_script}" "set(CASE_ROOT [==[${ENGINE_OUTPUT_DIR}/${name}]==])\n")
    file(
        APPEND "${_script}"
        [=[
set(ENGINE_ENABLE_RUNTIME_DEPS ON)
set(ENGINE_ENABLE_EDITOR_DEPS ON)
set(ENGINE_ENABLE_ASSET_PIPELINE_DEPS ON)
set(ENGINE_ENABLE_SCRIPT_RUNTIME ON)
set(ENGINE_ENABLE_V8 ON)
set(VCPKG_MANIFEST_FEATURES runtime)
]=])
    file(APPEND "${_script}" "${body}\n")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -P "${_script}"
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _stdout
        ERROR_VARIABLE _stderr
        TIMEOUT 20)
    file(WRITE "${ENGINE_OUTPUT_DIR}/${name}.log" "${_stdout}${_stderr}")
    if(expected_error STREQUAL "")
        if(NOT _result EQUAL 0)
            message(
                FATAL_ERROR
                    "Plugin selection case ${name} failed (${_result}):\n${_stdout}${_stderr}")
        endif()
    elseif(_result EQUAL 0 OR NOT "${_stdout}${_stderr}" MATCHES "${expected_error}")
        message(
            FATAL_ERROR
                "Plugin selection case ${name} did not reject with ${expected_error} (${_result}):\n${_stdout}${_stderr}"
        )
    endif()
endfunction()

run_case(
    empty_manifest
    ""
    [=[
set(ENGINE_RUNTIME_PLUGINS "" CACHE STRING "" FORCE)
set(ENGINE_EDITOR_PLUGINS "" CACHE STRING "" FORCE)
include("${ROOT}/cmake/plugins/selection.cmake")
if(ENGINE_SELECTED_PLUGINS)
    message(FATAL_ERROR "Explicit empty lists must select no plugins: ${ENGINE_SELECTED_PLUGINS}")
endif()
if("modeling" IN_LIST VCPKG_MANIFEST_FEATURES OR "font-import" IN_LIST VCPKG_MANIFEST_FEATURES OR
   "usd-import" IN_LIST VCPKG_MANIFEST_FEATURES)
    message(FATAL_ERROR "Empty plugin lists must not enable modeling or asset importer dependencies")
endif()
]=])

run_case(
    mcp_closure
    ""
    [=[
set(ENGINE_RUNTIME_PLUGINS "" CACHE STRING "" FORCE)
set(ENGINE_EDITOR_PLUGINS mcp CACHE STRING "" FORCE)
include("${ROOT}/cmake/plugins/selection.cmake")
if(NOT ENGINE_SELECTED_PLUGINS STREQUAL "runtime/mcp;editor/asset_build;editor/asset_sync;editor/mcp")
    message(FATAL_ERROR "MCP dependency closure/order differs: ${ENGINE_SELECTED_PLUGINS}")
endif()
if("modeling" IN_LIST VCPKG_MANIFEST_FEATURES)
    message(FATAL_ERROR "MCP alone must not enable independent modeling dependencies")
endif()
]=])

run_case(
    unknown_plugin
    "Unknown plugin: editor/missing"
    [=[
set(ENGINE_RUNTIME_PLUGINS "" CACHE STRING "" FORCE)
set(ENGINE_EDITOR_PLUGINS missing CACHE STRING "" FORCE)
include("${ROOT}/cmake/plugins/selection.cmake")
]=])

run_case(
    duplicate_registration
    "Duplicate plugin: runtime/duplicate"
    [=[
include("${ROOT}/cmake/plugins/registry.cmake")
function(configure_test_plugin)
endfunction()
engine_register_plugin(NAME runtime/duplicate CONFIGURE configure_test_plugin)
engine_register_plugin(NAME runtime/duplicate CONFIGURE configure_test_plugin)
]=])

run_case(
    dependency_cycle
    "Plugin dependency cycle:"
    [=[
include("${ROOT}/cmake/plugins/registry.cmake")
function(configure_test_plugin)
endfunction()
engine_register_plugin(NAME runtime/first CONFIGURE configure_test_plugin DEPENDS runtime/second)
engine_register_plugin(NAME runtime/second CONFIGURE configure_test_plugin DEPENDS runtime/first)
engine_resolve_plugin(runtime/first)
]=])

run_case(
    runtime_editor_dependency
    "Runtime plugin cannot depend on editor plugin:"
    [=[
include("${ROOT}/cmake/plugins/registry.cmake")
function(configure_test_plugin)
endfunction()
engine_register_plugin(NAME editor/tool CONFIGURE configure_test_plugin)
engine_register_plugin(NAME runtime/consumer CONFIGURE configure_test_plugin DEPENDS editor/tool)
engine_resolve_plugin(runtime/consumer)
]=])

run_case(
    android_defaults
    ""
    [=[
set(CMAKE_SYSTEM_NAME Android)
set(ANDROID_ABI arm64-v8a)
include("${ROOT}/cmake/plugins/selection.cmake")
if(NOT ENGINE_PLUGIN_DEVICE_BUILD OR
   NOT ENGINE_SELECTED_PLUGINS STREQUAL "runtime/resource_graph;runtime/animation_retarget;runtime/camera;runtime/character_controller;runtime/navigation_crowd;runtime/effects;runtime/terrain;runtime/destruction;runtime/script")
    message(FATAL_ERROR "Android defaults leaked editor plugins: ${ENGINE_SELECTED_PLUGINS}")
endif()
if(ENGINE_EDITOR_PLUGINS OR "modeling" IN_LIST VCPKG_MANIFEST_FEATURES OR
   "font-import" IN_LIST VCPKG_MANIFEST_FEATURES OR "usd-import" IN_LIST VCPKG_MANIFEST_FEATURES)
    message(FATAL_ERROR "Android must not prepare editor modeling or asset importer features")
endif()
]=])

run_case(
    terrain_editor_closure
    ""
    [=[
set(ENGINE_RUNTIME_PLUGINS "" CACHE STRING "" FORCE)
set(ENGINE_EDITOR_PLUGINS terrain CACHE STRING "" FORCE)
include("${ROOT}/cmake/plugins/selection.cmake")
if(NOT ENGINE_SELECTED_PLUGINS STREQUAL "runtime/terrain;editor/terrain")
    message(FATAL_ERROR "Terrain editor dependency closure differs: ${ENGINE_SELECTED_PLUGINS}")
endif()
]=])

run_case(
    scene_bake_without_resource_graph
    ""
    [=[
set(ENGINE_RUNTIME_PLUGINS "" CACHE STRING "" FORCE)
set(ENGINE_EDITOR_PLUGINS scene_bake CACHE STRING "" FORCE)
include("${ROOT}/cmake/plugins/selection.cmake")
if(NOT ENGINE_SELECTED_PLUGINS STREQUAL "editor/scene_bake")
    message(FATAL_ERROR "Scene baking must work without selecting a runtime loader plugin: ${ENGINE_SELECTED_PLUGINS}")
endif()
]=])

run_case(
    navigation_crowd_editor_closure
    ""
    [=[
set(ENGINE_RUNTIME_PLUGINS script CACHE STRING "" FORCE)
set(ENGINE_EDITOR_PLUGINS navigation_crowd CACHE STRING "" FORCE)
include("${ROOT}/cmake/plugins/selection.cmake")
if(NOT ENGINE_SELECTED_PLUGINS STREQUAL "runtime/navigation_crowd;runtime/script;editor/navigation_crowd")
    message(FATAL_ERROR "Navigation editor/script closure differs: ${ENGINE_SELECTED_PLUGINS}")
endif()
]=])

run_case(
    script_without_terrain
    ""
    [=[
set(ENGINE_RUNTIME_PLUGINS script CACHE STRING "" FORCE)
set(ENGINE_EDITOR_PLUGINS "" CACHE STRING "" FORCE)
include("${ROOT}/cmake/plugins/selection.cmake")
if(NOT ENGINE_SELECTED_PLUGINS STREQUAL "runtime/script")
    message(FATAL_ERROR "Script-only selection must not pull in terrain: ${ENGINE_SELECTED_PLUGINS}")
endif()
]=])

run_case(
    android_explicit_editor
    "Editor plugin is not available in a device build:"
    [=[
set(VCPKG_TARGET_TRIPLET arm64-android)
set(ENGINE_RUNTIME_PLUGINS "" CACHE STRING "" FORCE)
set(ENGINE_EDITOR_PLUGINS mcp CACHE STRING "" FORCE)
include("${ROOT}/cmake/plugins/selection.cmake")
]=])

run_case(
    modeling_feature_enabled
    ""
    [=[
set(ENGINE_RUNTIME_PLUGINS "" CACHE STRING "" FORCE)
set(ENGINE_EDITOR_PLUGINS modeling CACHE STRING "" FORCE)
include("${ROOT}/cmake/plugins/selection.cmake")
if(NOT ENGINE_SELECTED_PLUGINS STREQUAL "editor/modeling" OR
   NOT "modeling" IN_LIST VCPKG_MANIFEST_FEATURES OR
   NOT "runtime" IN_LIST VCPKG_MANIFEST_FEATURES)
    message(FATAL_ERROR "Selected modeling must add its feature and preserve caller features")
endif()
]=])

run_case(
    plugin_features_disabled
    ""
    [=[
# Reconfigure the same directory: plugins extend ordinary variables without changing cached user features.
file(MAKE_DIRECTORY "${CASE_ROOT}/source")
file(WRITE "${CASE_ROOT}/source/CMakeLists.txt"
    "cmake_minimum_required(VERSION 3.25)\n"
    "include([==[${ROOT}/cmake/plugins/selection.cmake]==])\n"
    "project(plugin_selection_fixture NONE)\n"
    "file(WRITE \"\${CMAKE_BINARY_DIR}/features.txt\" \"\${VCPKG_MANIFEST_FEATURES}\")\n")
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${CASE_ROOT}/source" -B "${CASE_ROOT}/build"
    -DENGINE_ENABLE_RUNTIME_DEPS=ON -DENGINE_ENABLE_EDITOR_DEPS=ON
    -DENGINE_ENABLE_ASSET_PIPELINE_DEPS=ON
    -DENGINE_RUNTIME_PLUGINS= "-DENGINE_EDITOR_PLUGINS=modeling;asset_import"
    -DVCPKG_MANIFEST_FEATURES=runtime
    RESULT_VARIABLE _first OUTPUT_VARIABLE _first_out ERROR_VARIABLE _first_error)
if(NOT _first EQUAL 0)
    message(FATAL_ERROR "First selection configure failed: ${_first_out}${_first_error}")
endif()
file(READ "${CASE_ROOT}/build/features.txt" _enabled)
if(NOT "modeling" IN_LIST _enabled OR NOT "font-import" IN_LIST _enabled OR
   NOT "usd-import" IN_LIST _enabled)
    message(FATAL_ERROR "First configure did not add modeling and asset importer features")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${CASE_ROOT}/source" -B "${CASE_ROOT}/build"
    -DENGINE_EDITOR_PLUGINS=
    RESULT_VARIABLE _second OUTPUT_VARIABLE _second_out ERROR_VARIABLE _second_error)
if(NOT _second EQUAL 0)
    message(FATAL_ERROR "Second selection configure failed: ${_second_out}${_second_error}")
endif()
file(READ "${CASE_ROOT}/build/features.txt" _disabled)
if(NOT _disabled STREQUAL "runtime")
    message(FATAL_ERROR "Deselected plugins leaked an auto-added feature across configures: ${_disabled}")
endif()
]=])

run_case(
    explicit_user_feature
    ""
    [=[
set(VCPKG_MANIFEST_FEATURES "runtime;modeling;font-import")
set(ENGINE_RUNTIME_PLUGINS "" CACHE STRING "" FORCE)
set(ENGINE_EDITOR_PLUGINS "" CACHE STRING "" FORCE)
include("${ROOT}/cmake/plugins/selection.cmake")
if(NOT VCPKG_MANIFEST_FEATURES STREQUAL "runtime;modeling;font-import")
    message(FATAL_ERROR "Explicit caller features must not be silently removed")
endif()
]=])

run_case(
    required_option
    "requires ENGINE_ENABLE_RUNTIME_DEPS"
    [=[
set(ENGINE_ENABLE_RUNTIME_DEPS OFF)
set(ENGINE_RUNTIME_PLUGINS resource_graph CACHE STRING "" FORCE)
set(ENGINE_EDITOR_PLUGINS "" CACHE STRING "" FORCE)
include("${ROOT}/cmake/plugins/selection.cmake")
]=])

run_case(
    configure_order
    ""
    [=[
include("${ROOT}/cmake/plugins/registry.cmake")
function(configure_dependency)
    set_property(GLOBAL APPEND PROPERTY CONFIGURE_CALLS dependency)
endfunction()
function(configure_user)
    set_property(GLOBAL APPEND PROPERTY CONFIGURE_CALLS user)
endfunction()
engine_register_plugin(NAME runtime/dependency CONFIGURE configure_dependency)
engine_register_plugin(NAME runtime/user CONFIGURE configure_user DEPENDS runtime/dependency)
engine_resolve_plugin(runtime/user)
engine_resolve_plugin(runtime/dependency)
get_property(ENGINE_SELECTED_PLUGINS GLOBAL PROPERTY ENGINE_RESOLVED_PLUGINS)
engine_configure_selected_plugins()
get_property(_calls GLOBAL PROPERTY CONFIGURE_CALLS)
if(NOT _calls STREQUAL "dependency;user")
    message(FATAL_ERROR "Configuration must run each selected plugin once, dependencies first: ${_calls}")
endif()
]=])

run_case(
    script_disabled
    ""
    [=[
set(ENGINE_RUNTIME_PLUGINS resource_graph CACHE STRING "" FORCE)
set(ENGINE_EDITOR_PLUGINS "" CACHE STRING "" FORCE)
include("${ROOT}/cmake/plugins/selection.cmake")
if("runtime/script" IN_LIST ENGINE_SELECTED_PLUGINS OR "quickjs" IN_LIST VCPKG_MANIFEST_FEATURES)
    message(FATAL_ERROR "Disabled scripting must not select a VM dependency")
endif()
]=])

run_case(
    script_default_quickjs
    ""
    [=[
set(ENGINE_RUNTIME_PLUGINS script CACHE STRING "" FORCE)
set(ENGINE_EDITOR_PLUGINS "" CACHE STRING "" FORCE)
include("${ROOT}/cmake/plugins/selection.cmake")
if(NOT ENGINE_SCRIPT_BACKEND STREQUAL "quickjs" OR ENGINE_ENABLE_V8 OR
   NOT "quickjs" IN_LIST VCPKG_MANIFEST_FEATURES)
    message(FATAL_ERROR "Default script backend must select only QuickJS, even with old V8 cache")
endif()
]=])

run_case(
    script_explicit_v8
    ""
    [=[
set(ENGINE_SCRIPT_BACKEND v8 CACHE STRING "" FORCE)
set(ENGINE_ENABLE_V8 OFF)
set(VCPKG_MANIFEST_FEATURES "runtime;quickjs")
set(ENGINE_RUNTIME_PLUGINS script CACHE STRING "" FORCE)
set(ENGINE_EDITOR_PLUGINS "" CACHE STRING "" FORCE)
include("${ROOT}/cmake/plugins/selection.cmake")
if(NOT ENGINE_ENABLE_V8 OR "quickjs" IN_LIST VCPKG_MANIFEST_FEATURES)
    message(FATAL_ERROR "V8 script backend must not select QuickJS dependencies")
endif()
]=])

run_case(
    script_invalid_backend
    "ENGINE_SCRIPT_BACKEND must be quickjs or v8"
    [=[
set(ENGINE_SCRIPT_BACKEND both CACHE STRING "" FORCE)
include("${ROOT}/cmake/plugins/selection.cmake")
]=])

run_case(
    script_switch_backend
    ""
    [=[
file(MAKE_DIRECTORY "${CASE_ROOT}/source")
file(WRITE "${CASE_ROOT}/source/CMakeLists.txt"
    "cmake_minimum_required(VERSION 3.25)\n"
    "include([==[${ROOT}/cmake/plugins/selection.cmake]==])\n"
    "project(script_selection_fixture NONE)\n"
    "file(WRITE \"\${CMAKE_BINARY_DIR}/selection.txt\" \"\${VCPKG_MANIFEST_FEATURES}|\${ENGINE_ENABLE_V8}\")\n")
foreach(_backend quickjs v8 quickjs)
    execute_process(COMMAND "${CMAKE_COMMAND}" -S "${CASE_ROOT}/source" -B "${CASE_ROOT}/build"
        -DENGINE_ENABLE_RUNTIME_DEPS=ON -DENGINE_ENABLE_V8=ON
        -DENGINE_RUNTIME_PLUGINS=script -DENGINE_EDITOR_PLUGINS=
        "-DENGINE_SCRIPT_BACKEND=${_backend}" -DVCPKG_MANIFEST_FEATURES=runtime
        RESULT_VARIABLE _result OUTPUT_VARIABLE _out ERROR_VARIABLE _error)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "Script backend switch failed: ${_out}${_error}")
    endif()
    file(READ "${CASE_ROOT}/build/selection.txt" _selection)
    if(_backend STREQUAL "quickjs")
        set(_expected "runtime;quickjs|OFF")
    else()
        set(_expected "runtime|ON")
    endif()
    if(NOT _selection STREQUAL _expected)
        message(FATAL_ERROR "Backend ${_backend} left stale dependencies: ${_selection}")
    endif()
endforeach()
]=])

run_case(
    character_controller_editor_closure
    ""
    [=[
set(ENGINE_ENABLE_EDITOR_DEPS OFF)
set(ENGINE_RUNTIME_PLUGINS "" CACHE STRING "" FORCE)
set(ENGINE_EDITOR_PLUGINS character_controller CACHE STRING "" FORCE)
include("${ROOT}/cmake/plugins/selection.cmake")
if(NOT ENGINE_SELECTED_PLUGINS STREQUAL "runtime/camera;runtime/character_controller;editor/camera;editor/character_controller")
    message(FATAL_ERROR "Controller codec must select its runtime without unrelated plugins: ${ENGINE_SELECTED_PLUGINS}")
endif()
]=])

run_case(
    unified_asset_import
    ""
    [=[
set(ENGINE_RUNTIME_PLUGINS "" CACHE STRING "" FORCE)
set(ENGINE_EDITOR_PLUGINS asset_import CACHE STRING "" FORCE)
include("${ROOT}/cmake/plugins/selection.cmake")
if(NOT ENGINE_SELECTED_PLUGINS STREQUAL "editor/asset_import" OR
   NOT "font-import" IN_LIST VCPKG_MANIFEST_FEATURES OR
   NOT "usd-import" IN_LIST VCPKG_MANIFEST_FEATURES OR
   "modeling" IN_LIST VCPKG_MANIFEST_FEATURES)
    message(FATAL_ERROR "Asset import must select one plugin and both format dependency features: ${ENGINE_SELECTED_PLUGINS} / ${VCPKG_MANIFEST_FEATURES}")
endif()
]=])

run_case(
    ui_asset_import_closure
    ""
    [=[
set(ENGINE_RUNTIME_PLUGINS "" CACHE STRING "" FORCE)
set(ENGINE_EDITOR_PLUGINS ui CACHE STRING "" FORCE)
include("${ROOT}/cmake/plugins/selection.cmake")
if(NOT ENGINE_SELECTED_PLUGINS STREQUAL "editor/asset_import;editor/ui" OR
   NOT "font-import" IN_LIST VCPKG_MANIFEST_FEATURES OR
   NOT "usd-import" IN_LIST VCPKG_MANIFEST_FEATURES)
    message(FATAL_ERROR "UI editing must depend on the unified asset importer: ${ENGINE_SELECTED_PLUGINS} / ${VCPKG_MANIFEST_FEATURES}")
endif()
]=])

run_case(
    legacy_import_plugin_rejected
    "Unknown plugin: editor/font_import"
    [=[
set(ENGINE_RUNTIME_PLUGINS "" CACHE STRING "" FORCE)
set(ENGINE_EDITOR_PLUGINS font_import CACHE STRING "" FORCE)
include("${ROOT}/cmake/plugins/selection.cmake")
]=])

run_case(
    asset_import_target_toggle
    ""
    [=[
# Use actual unified plugin and host assembly functions; stub external dependencies without building them.
set(_source "${CASE_ROOT}/source")
file(MAKE_DIRECTORY "${_source}/src/engine/plugins/editor/asset_import"
                    "${_source}/third_party/ufbx")
file(GLOB _import_sources "${ROOT}/src/engine/plugins/editor/asset_import/*.cpp")
foreach(_path IN LISTS _import_sources)
    get_filename_component(_name "${_path}" NAME)
    configure_file("${_path}"
        "${_source}/src/engine/plugins/editor/asset_import/${_name}" COPYONLY)
endforeach()
file(WRITE "${_source}/third_party/ufbx/ufbx.c" "/* external dependency fixture */\n")
file(WRITE "${_source}/probe.cpp" "int main() { return 0; }\n")
file(WRITE "${_source}/CMakeLists.txt"
    "cmake_minimum_required(VERSION 3.25)\n"
    "set(ROOT [==[${ROOT}]==])\n")
file(APPEND "${_source}/CMakeLists.txt" [==[
include("${ROOT}/cmake/plugins/selection.cmake")
project(asset_import_selection_fixture LANGUAGES C CXX)
function(engine_apply_options target)
endfunction()
foreach(_name asset_pipeline resource)
    add_library(engine_${_name} INTERFACE)
    add_library(engine::${_name} ALIAS engine_${_name})
endforeach()
foreach(_name engine_editor_mesh engine_usd_sdk ozz_animation_offline)
    add_library(${_name} INTERFACE)
endforeach()
foreach(_name Freetype::Freetype nlohmann_json::nlohmann_json tomlplusplus::tomlplusplus)
    add_library(${_name} INTERFACE IMPORTED)
endforeach()
set_target_properties(tomlplusplus::tomlplusplus PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${ROOT}/include")
engine_configure_selected_plugins()
set(ENGINE_BUILD_APP OFF)
include("${ROOT}/cmake/editor/hosts.cmake")
add_executable(probe_host probe.cpp)
engine_link_asset_importers(probe_host)
get_target_property(_host_definitions probe_host COMPILE_DEFINITIONS)
get_target_property(_host_links probe_host LINK_LIBRARIES)
foreach(_format fbx usd font ui json)
    if(TARGET engine_${_format}_import OR TARGET engine::${_format}_import)
        message(FATAL_ERROR "Legacy importer target leaked: ${_format}")
    endif()
endforeach()
if("editor/asset_import" IN_LIST ENGINE_SELECTED_PLUGINS)
    if(NOT TARGET engine_asset_import OR NOT TARGET engine::asset_import)
        message(FATAL_ERROR "Enabled unified asset importer target or alias is missing")
    endif()
    get_target_property(_links engine_asset_import LINK_LIBRARIES)
    get_target_property(_sources engine_asset_import SOURCES)
    foreach(_dependency engine::asset_pipeline engine::resource engine_ufbx
                        engine_usd_sdk ozz_animation_offline engine_editor_mesh
                        Freetype::Freetype nlohmann_json::nlohmann_json)
        if(NOT "${_dependency}" IN_LIST _links)
            message(FATAL_ERROR "Unified asset import missing actual target dependency: ${_dependency}")
        endif()
    endforeach()
    foreach(_format fbx usd font ui json)
        set(_found FALSE)
        foreach(_path IN LISTS _sources)
            if(_path MATCHES "/${_format}_importer\\.cpp$")
                set(_found TRUE)
            endif()
        endforeach()
        if(NOT _found)
            message(FATAL_ERROR "Unified target does not compile ${_format}_importer.cpp")
        endif()
    endforeach()
    set(_host_import_links "${_host_links}")
    list(FILTER _host_import_links INCLUDE REGEX "^engine::asset_import$")
    list(LENGTH _host_import_links _host_import_count)
    if(NOT _host_import_count EQUAL 1 OR
       NOT "ENGINE_HAS_ASSET_IMPORT_PLUGIN=1" IN_LIST _host_definitions)
        message(FATAL_ERROR "Unified importer must have one host link and enabled assembly macro")
    endif()
elseif(TARGET engine_asset_import OR TARGET engine::asset_import OR
       TARGET engine_ufbx OR
       "engine::asset_import" IN_LIST _host_links OR
       NOT "ENGINE_HAS_ASSET_IMPORT_PLUGIN=0" IN_LIST _host_definitions)
    message(FATAL_ERROR "Disabled asset importer leaked targets, dependencies or host linkage")
endif()
file(WRITE "${CMAKE_BINARY_DIR}/selection.txt"
    "${ENGINE_SELECTED_PLUGINS}|${VCPKG_MANIFEST_FEATURES}")
]==])
foreach(_state enabled disabled)
    if(_state STREQUAL "enabled")
        set(_plugins asset_import)
    else()
        set(_plugins "")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -S "${_source}" -B "${CASE_ROOT}/build"
        -DENGINE_ENABLE_RUNTIME_DEPS=ON -DENGINE_ENABLE_EDITOR_DEPS=ON
        -DENGINE_ENABLE_ASSET_PIPELINE_DEPS=ON -DENGINE_ENABLE_SCRIPT_RUNTIME=OFF
        -DENGINE_RUNTIME_PLUGINS= "-DENGINE_EDITOR_PLUGINS=${_plugins}"
        -DVCPKG_MANIFEST_FEATURES=runtime
        RESULT_VARIABLE _result OUTPUT_VARIABLE _out ERROR_VARIABLE _error)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "Asset importer toggle configure failed: ${_out}${_error}")
    endif()
    file(READ "${CASE_ROOT}/build/selection.txt" _selection)
    if(_plugins STREQUAL "asset_import")
        if(NOT _selection MATCHES "^editor/asset_import\\|" OR
           NOT _selection MATCHES "font-import" OR NOT _selection MATCHES "usd-import")
            message(FATAL_ERROR "Enabled unified asset import omitted its selection or dependency features: ${_selection}")
        endif()
    elseif(NOT _selection STREQUAL "|runtime")
        message(FATAL_ERROR "Disabled asset importer left stale selection/features: ${_selection}")
    endif()
endforeach()
]=])

run_case(
    disabled_v8_clears_icu
    ""
    [=[
set(ENGINE_ENABLE_V8 OFF)
set(ENGINE_V8_ICU_DATA stale-icu-data CACHE INTERNAL "" FORCE)
include("${ROOT}/cmake/dependencies/v8.cmake")
if(ENGINE_V8_ICU_DATA)
    message(FATAL_ERROR "Disabled V8 must clear deployment metadata from the previous backend")
endif()
]=])

message(STATUS "Plugin selection regression cases passed")

run_case(
    destruction_editor_closure
    ""
    [=[
set(ENGINE_RUNTIME_PLUGINS "" CACHE STRING "" FORCE)
set(ENGINE_EDITOR_PLUGINS destruction CACHE STRING "" FORCE)
include("${ROOT}/cmake/plugins/selection.cmake")
if(NOT ENGINE_SELECTED_PLUGINS STREQUAL "runtime/destruction;editor/destruction" OR
   NOT "destruction-authoring" IN_LIST VCPKG_MANIFEST_FEATURES)
    message(FATAL_ERROR "Destruction editor closure is incomplete: ${ENGINE_SELECTED_PLUGINS}")
endif()
]=])
run_case(
    destruction_runtime_only
    ""
    [=[
set(ENGINE_RUNTIME_PLUGINS destruction CACHE STRING "" FORCE)
set(ENGINE_EDITOR_PLUGINS "" CACHE STRING "" FORCE)
include("${ROOT}/cmake/plugins/selection.cmake")
if(NOT ENGINE_SELECTED_PLUGINS STREQUAL "runtime/destruction" OR
   "destruction-authoring" IN_LIST VCPKG_MANIFEST_FEATURES)
    message(FATAL_ERROR "Runtime destruction must not pull in authoring")
endif()
]=])

run_case(
    animation_retarget_independent
    ""
    [=[
set(ENGINE_RUNTIME_PLUGINS animation_retarget CACHE STRING "" FORCE)
set(ENGINE_EDITOR_PLUGINS "" CACHE STRING "" FORCE)
include("${ROOT}/cmake/plugins/selection.cmake")
if(NOT ENGINE_SELECTED_PLUGINS STREQUAL "runtime/animation_retarget")
    message(FATAL_ERROR "Retarget runtime unexpectedly pulls script/editor plugins: ${ENGINE_SELECTED_PLUGINS}")
endif()
]=])

run_case(
    animation_authoring_closure
    ""
    [=[
set(ENGINE_RUNTIME_PLUGINS "" CACHE STRING "" FORCE)
set(ENGINE_EDITOR_PLUGINS animation_tools CACHE STRING "" FORCE)
include("${ROOT}/cmake/plugins/selection.cmake")
if(NOT ENGINE_SELECTED_PLUGINS STREQUAL "editor/asset_import;editor/animation_tools")
    message(FATAL_ERROR "Animation tools must only reuse the explicit FBX importer: ${ENGINE_SELECTED_PLUGINS}")
endif()
]=])
