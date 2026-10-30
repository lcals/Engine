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
    file(GLOB _bindings CONFIGURE_DEPENDS
         "${PROJECT_SOURCE_DIR}/src/engine/plugins/runtime/script/bindings/*_binding.cpp")
    target_sources(
        engine_script
        PRIVATE ${_bindings}
                src/engine/plugins/runtime/script/bindings/binding_context.cpp
                src/engine/plugins/runtime/script/bindings/script_binding_registry.cpp
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
        PRIVATE engine::app
                engine::log
                engine::resource
                engine::world
                engine::render
                engine::ui
                SDL3::SDL3
                nlohmann_json::nlohmann_json)
    target_compile_definitions(engine_script PRIVATE JSON_NOEXCEPTION=1)
endfunction()

engine_register_plugin(NAME runtime/script CONFIGURE engine_configure_script_plugin OPTIONS
                       ENGINE_ENABLE_RUNTIME_DEPS)
