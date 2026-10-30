function(engine_configure_script_plugin)
    engine_add_system(plugins/runtime script)
    file(GLOB _bindings CONFIGURE_DEPENDS
         "${PROJECT_SOURCE_DIR}/src/engine/plugins/runtime/script/bindings/*_binding.cpp")
    target_sources(
        engine_script
        PRIVATE ${_bindings} src/engine/plugins/runtime/script/bindings/binding_context.cpp
                src/engine/plugins/runtime/script/bindings/script_binding_registry.cpp)
    target_include_directories(engine_script PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    target_link_libraries(
        engine_script
        PRIVATE engine::v8
                engine::app
                engine::log
                engine::resource
                engine::world
                engine::render
                engine::ui
                SDL3::SDL3
                nlohmann_json::nlohmann_json)
    target_compile_definitions(engine_script PRIVATE JSON_NOEXCEPTION=1)
endfunction()

engine_register_plugin(
    NAME
    runtime/script
    CONFIGURE
    engine_configure_script_plugin
    OPTIONS
    ENGINE_ENABLE_V8
    ENGINE_ENABLE_RUNTIME_DEPS)
