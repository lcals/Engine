# 每个宿主只链接一个 VM；业务绑定与核心契约保持一致。
set(ENGINE_SCRIPT_BACKEND
    quickjs
    CACHE STRING "JavaScript backend: quickjs or v8")
set_property(CACHE ENGINE_SCRIPT_BACKEND PROPERTY STRINGS quickjs v8)
if(NOT ENGINE_SCRIPT_BACKEND MATCHES "^(quickjs|v8)$")
    message(FATAL_ERROR "ENGINE_SCRIPT_BACKEND must be quickjs or v8")
endif()

function(engine_configure_script_plugin)
    engine_add_system(plugins/runtime script)
    engine_enable_profiling(engine_script)
    file(GLOB _bindings CONFIGURE_DEPENDS
         "${PROJECT_SOURCE_DIR}/src/engine/plugins/runtime/script/bindings/*_binding.cpp")
    target_sources(
        engine_script
        PRIVATE ${_bindings}
                src/engine/plugins/runtime/script/bindings/binding_context.cpp
                src/engine/plugins/runtime/script/bindings/script_binding_registry.cpp
                src/engine/plugins/runtime/script/bindings/completion_binding.cpp
                src/engine/plugins/runtime/script/vm/script_vm.cpp
                "src/engine/plugins/runtime/script/vm/${ENGINE_SCRIPT_BACKEND}_vm.cpp")
    if(ENGINE_SCRIPT_BACKEND STREQUAL "quickjs")
        target_link_libraries(engine_script PRIVATE qjs)
        target_compile_definitions(engine_script PUBLIC ENGINE_SCRIPT_BACKEND_QUICKJS=1)
    else()
        target_link_libraries(engine_script PRIVATE engine::v8)
        target_compile_definitions(engine_script PUBLIC ENGINE_SCRIPT_BACKEND_V8=1)
    endif()
    target_include_directories(engine_script PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    target_link_libraries(
        engine_script
        PRIVATE engine::movie
                engine::app
                engine::storage
                engine::input
                engine::log
                engine::resource
                engine::world
                engine::animation
                engine::physics
                engine::navigation
                engine::window
                engine::render
                engine::ui
                SDL3::SDL3
                nlohmann_json::nlohmann_json)
    target_compile_definitions(engine_script PRIVATE JSON_NOEXCEPTION=1)
    if("runtime/navigation_crowd" IN_LIST ENGINE_SELECTED_PLUGINS)
        target_link_libraries(engine_script PRIVATE engine::navigation_crowd)
        target_compile_definitions(engine_script PRIVATE ENGINE_HAS_NAVIGATION_CROWD_PLUGIN=1)
    else()
        target_compile_definitions(engine_script PRIVATE ENGINE_HAS_NAVIGATION_CROWD_PLUGIN=0)
    endif()
    if("runtime/camera" IN_LIST ENGINE_SELECTED_PLUGINS)
        target_link_libraries(engine_script PRIVATE engine::camera)
        target_compile_definitions(engine_script PRIVATE ENGINE_HAS_CAMERA_PLUGIN=1)
    else()
        target_compile_definitions(engine_script PRIVATE ENGINE_HAS_CAMERA_PLUGIN=0)
    endif()
    if("runtime/character_controller" IN_LIST ENGINE_SELECTED_PLUGINS)
        target_link_libraries(engine_script PRIVATE engine::character_controller)
        target_compile_definitions(engine_script PRIVATE ENGINE_HAS_CHARACTER_CONTROLLER_PLUGIN=1)
    else()
        target_compile_definitions(engine_script PRIVATE ENGINE_HAS_CHARACTER_CONTROLLER_PLUGIN=0)
    endif()
    if("runtime/effects" IN_LIST ENGINE_SELECTED_PLUGINS)
        target_link_libraries(engine_script PRIVATE engine::effects)
        target_compile_definitions(engine_script PRIVATE ENGINE_HAS_EFFECTS_PLUGIN=1)
    else()
        target_compile_definitions(engine_script PRIVATE ENGINE_HAS_EFFECTS_PLUGIN=0)
    endif()
    if("runtime/terrain" IN_LIST ENGINE_SELECTED_PLUGINS)
        target_link_libraries(engine_script PRIVATE engine::terrain)
        target_compile_definitions(engine_script PRIVATE ENGINE_HAS_TERRAIN_PLUGIN=1)
    else()
        target_compile_definitions(engine_script PRIVATE ENGINE_HAS_TERRAIN_PLUGIN=0)
    endif()
    if("runtime/destruction" IN_LIST ENGINE_SELECTED_PLUGINS)
        target_link_libraries(engine_script PRIVATE engine::destruction)
        target_compile_definitions(engine_script PRIVATE ENGINE_HAS_DESTRUCTION_PLUGIN=1)
    else()
        target_compile_definitions(engine_script PRIVATE ENGINE_HAS_DESTRUCTION_PLUGIN=0)
    endif()
endfunction()

# Bindings depend only on explicitly selected/default terrain, without forcing it into a
# script-only plugin selection. The dependency is owned by this plugin manifest.
set(_script_plugin_dependencies)
if((NOT DEFINED ENGINE_RUNTIME_PLUGINS AND ENGINE_ENABLE_RUNTIME_DEPS)
   OR "camera" IN_LIST ENGINE_RUNTIME_PLUGINS
   OR "camera" IN_LIST ENGINE_EDITOR_PLUGINS
   OR "character_controller" IN_LIST ENGINE_RUNTIME_PLUGINS
   OR "character_controller" IN_LIST ENGINE_EDITOR_PLUGINS)
    list(APPEND _script_plugin_dependencies runtime/camera)
endif()
if((NOT DEFINED ENGINE_RUNTIME_PLUGINS AND ENGINE_ENABLE_RUNTIME_DEPS)
   OR (NOT DEFINED ENGINE_EDITOR_PLUGINS AND ENGINE_ENABLE_ASSET_PIPELINE_DEPS)
   OR "navigation_crowd" IN_LIST ENGINE_RUNTIME_PLUGINS
   OR "navigation_crowd" IN_LIST ENGINE_EDITOR_PLUGINS)
    list(APPEND _script_plugin_dependencies runtime/navigation_crowd)
endif()
if((NOT DEFINED ENGINE_RUNTIME_PLUGINS AND ENGINE_ENABLE_RUNTIME_DEPS)
   OR "character_controller" IN_LIST ENGINE_RUNTIME_PLUGINS
   OR "character_controller" IN_LIST ENGINE_EDITOR_PLUGINS)
    list(APPEND _script_plugin_dependencies runtime/character_controller)
endif()
if((NOT DEFINED ENGINE_RUNTIME_PLUGINS AND ENGINE_ENABLE_RUNTIME_DEPS)
   OR "terrain" IN_LIST ENGINE_RUNTIME_PLUGINS
   OR "terrain" IN_LIST ENGINE_EDITOR_PLUGINS)
    list(APPEND _script_plugin_dependencies runtime/terrain)
endif()
if((NOT DEFINED ENGINE_RUNTIME_PLUGINS AND ENGINE_ENABLE_RUNTIME_DEPS)
   OR "destruction" IN_LIST ENGINE_RUNTIME_PLUGINS
   OR "destruction" IN_LIST ENGINE_EDITOR_PLUGINS)
    list(APPEND _script_plugin_dependencies runtime/destruction)
endif()
if((NOT DEFINED ENGINE_RUNTIME_PLUGINS AND ENGINE_ENABLE_RUNTIME_DEPS)
   OR "effects" IN_LIST ENGINE_RUNTIME_PLUGINS
   OR "effects" IN_LIST ENGINE_EDITOR_PLUGINS)
    list(APPEND _script_plugin_dependencies runtime/effects)
endif()
engine_register_plugin(
    NAME
    runtime/script
    CONFIGURE
    engine_configure_script_plugin
    DEPENDS
    ${_script_plugin_dependencies}
    OPTIONS
    ENGINE_ENABLE_RUNTIME_DEPS)
