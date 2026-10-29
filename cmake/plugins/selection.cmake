include("${CMAKE_CURRENT_LIST_DIR}/registry.cmake")

# 清单文件同时拥有该插件的源码、依赖和目标定义，不另维护第二份描述。
include("${CMAKE_CURRENT_LIST_DIR}/runtime/resource_graph.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/editor/modeling.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/editor/asset_build.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/editor/asset_sync.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/editor/mcp.cmake")

# project() 前 Android 工具链可能尚未设置 ANDROID。
set(ENGINE_PLUGIN_DEVICE_BUILD OFF)
if(ANDROID
   OR ANDROID_ABI
   OR VCPKG_TARGET_TRIPLET MATCHES "android|ios"
   OR CMAKE_SYSTEM_NAME MATCHES "Android|iOS")
    set(ENGINE_PLUGIN_DEVICE_BUILD ON)
endif()
set(_runtime_plugins "")
set(_editor_plugins "")
if(ENGINE_ENABLE_RUNTIME_DEPS)
    list(APPEND _runtime_plugins resource_graph)
endif()
if(NOT ENGINE_PLUGIN_DEVICE_BUILD)
    if(ENGINE_ENABLE_EDITOR_DEPS OR ENGINE_ENABLE_ASSET_PIPELINE_DEPS)
        list(APPEND _editor_plugins modeling)
    endif()
    if(ENGINE_ENABLE_ASSET_PIPELINE_DEPS)
        list(APPEND _editor_plugins asset_build)
    endif()
    if(ENGINE_ENABLE_SCRIPT_RUNTIME AND (ENGINE_ENABLE_EDITOR_DEPS
                                         OR ENGINE_ENABLE_ASSET_PIPELINE_DEPS))
        list(APPEND _editor_plugins asset_sync)
        if(ENGINE_ENABLE_ASSET_PIPELINE_DEPS)
            list(APPEND _editor_plugins mcp)
        endif()
    endif()
endif()
set(ENGINE_RUNTIME_PLUGINS
    "${_runtime_plugins}"
    CACHE STRING "Runtime plugin names")
set(ENGINE_EDITOR_PLUGINS
    "${_editor_plugins}"
    CACHE STRING "Editor plugin names")

foreach(_scope runtime editor)
    string(TOUPPER "${_scope}" _scope_upper)
    foreach(_name IN LISTS ENGINE_${_scope_upper}_PLUGINS)
        engine_resolve_plugin("${_scope}/${_name}")
    endforeach()
endforeach()
get_property(ENGINE_SELECTED_PLUGINS GLOBAL PROPERTY ENGINE_RESOLVED_PLUGINS)

if("editor/modeling" IN_LIST ENGINE_SELECTED_PLUGINS
   AND NOT ENGINE_ENABLE_EDITOR_DEPS
   AND NOT ENGINE_ENABLE_ASSET_PIPELINE_DEPS)
    message(FATAL_ERROR "Modeling requires editor or asset pipeline dependencies")
endif()
foreach(_plugin IN LISTS ENGINE_SELECTED_PLUGINS)
    get_property(_features GLOBAL PROPERTY "ENGINE_PLUGIN_${_plugin}_VCPKG_FEATURES")
    list(APPEND VCPKG_MANIFEST_FEATURES ${_features})
endforeach()
if(VCPKG_MANIFEST_FEATURES)
    list(REMOVE_DUPLICATES VCPKG_MANIFEST_FEATURES)
endif()
message(STATUS "Engine plugins: ${ENGINE_SELECTED_PLUGINS}")
