function(engine_configure_ui_import_plugin)
    add_library(engine_ui_import STATIC src/engine/plugins/editor/ui_import/ui_importer.cpp)
    add_library(engine::ui_import ALIAS engine_ui_import)
    target_include_directories(engine_ui_import PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    get_target_property(_ui_toml_includes tomlplusplus::tomlplusplus INTERFACE_INCLUDE_DIRECTORIES)
    target_include_directories(engine_ui_import SYSTEM PRIVATE ${_ui_toml_includes})
    target_link_libraries(engine_ui_import PUBLIC engine::asset_pipeline PRIVATE engine::resource)
    target_compile_definitions(
        engine_ui_import PRIVATE TOML_HEADER_ONLY=1 TOML_EXCEPTIONS=0
                                 ENGINE_SCHEMA_SOURCE_DIR="${PROJECT_SOURCE_DIR}/include/schemas/runtime")
    engine_apply_options(engine_ui_import)
endfunction()

engine_register_plugin(NAME editor/ui_import CONFIGURE engine_configure_ui_import_plugin OPTIONS
                       ENGINE_ENABLE_ASSET_PIPELINE_DEPS)
