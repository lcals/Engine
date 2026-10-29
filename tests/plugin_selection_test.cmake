cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED ENGINE_SOURCE_DIR)
    get_filename_component(ENGINE_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
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
if("modeling" IN_LIST VCPKG_MANIFEST_FEATURES)
    message(FATAL_ERROR "Empty plugin lists must not enable modeling dependencies")
endif()
]=])

run_case(
    mcp_closure
    ""
    [=[
set(ENGINE_RUNTIME_PLUGINS "" CACHE STRING "" FORCE)
set(ENGINE_EDITOR_PLUGINS mcp CACHE STRING "" FORCE)
include("${ROOT}/cmake/plugins/selection.cmake")
if(NOT ENGINE_SELECTED_PLUGINS STREQUAL "editor/asset_build;editor/asset_sync;editor/mcp")
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
   NOT ENGINE_SELECTED_PLUGINS STREQUAL "runtime/resource_graph")
    message(FATAL_ERROR "Android defaults leaked editor plugins: ${ENGINE_SELECTED_PLUGINS}")
endif()
if(ENGINE_EDITOR_PLUGINS OR "modeling" IN_LIST VCPKG_MANIFEST_FEATURES)
    message(FATAL_ERROR "Android must not prepare editor modeling features")
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
    modeling_feature_disabled
    ""
    [=[
# 同一配置目录真实重配：插件自动增加普通变量，不污染用户 feature 的 CACHE 初值。
file(MAKE_DIRECTORY "${CASE_ROOT}/source")
file(WRITE "${CASE_ROOT}/source/CMakeLists.txt"
    "cmake_minimum_required(VERSION 3.25)\n"
    "include([==[${ROOT}/cmake/plugins/selection.cmake]==])\n"
    "project(plugin_selection_fixture NONE)\n"
    "file(WRITE \"\${CMAKE_BINARY_DIR}/features.txt\" \"\${VCPKG_MANIFEST_FEATURES}\")\n")
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${CASE_ROOT}/source" -B "${CASE_ROOT}/build"
    -DENGINE_ENABLE_RUNTIME_DEPS=ON -DENGINE_ENABLE_EDITOR_DEPS=ON
    -DENGINE_RUNTIME_PLUGINS= -DENGINE_EDITOR_PLUGINS=modeling
    -DVCPKG_MANIFEST_FEATURES=runtime
    RESULT_VARIABLE _first OUTPUT_VARIABLE _first_out ERROR_VARIABLE _first_error)
if(NOT _first EQUAL 0)
    message(FATAL_ERROR "First selection configure failed: ${_first_out}${_first_error}")
endif()
file(READ "${CASE_ROOT}/build/features.txt" _enabled)
if(NOT "modeling" IN_LIST _enabled)
    message(FATAL_ERROR "First configure did not add modeling feature")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${CASE_ROOT}/source" -B "${CASE_ROOT}/build"
    -DENGINE_EDITOR_PLUGINS=
    RESULT_VARIABLE _second OUTPUT_VARIABLE _second_out ERROR_VARIABLE _second_error)
if(NOT _second EQUAL 0)
    message(FATAL_ERROR "Second selection configure failed: ${_second_out}${_second_error}")
endif()
file(READ "${CASE_ROOT}/build/features.txt" _disabled)
if(NOT _disabled STREQUAL "runtime")
    message(FATAL_ERROR "Deselected modeling leaked an auto-added feature across configures: ${_disabled}")
endif()
]=])

run_case(
    explicit_user_feature
    ""
    [=[
set(VCPKG_MANIFEST_FEATURES "runtime;modeling")
set(ENGINE_RUNTIME_PLUGINS "" CACHE STRING "" FORCE)
set(ENGINE_EDITOR_PLUGINS "" CACHE STRING "" FORCE)
include("${ROOT}/cmake/plugins/selection.cmake")
if(NOT VCPKG_MANIFEST_FEATURES STREQUAL "runtime;modeling")
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

message(STATUS "Plugin selection regression cases passed")
