function(engine_configure_font_import_plugin)
    add_library(engine_font_import STATIC src/engine/plugins/editor/font_import/font_importer.cpp)
    add_library(engine::font_import ALIAS engine_font_import)
    target_include_directories(engine_font_import PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    get_target_property(_font_toml_includes tomlplusplus::tomlplusplus INTERFACE_INCLUDE_DIRECTORIES)
    target_include_directories(engine_font_import SYSTEM PRIVATE ${_font_toml_includes})
    target_link_libraries(engine_font_import PUBLIC engine::asset_pipeline
                          PRIVATE engine::resource Freetype::Freetype)
    target_compile_definitions(
        engine_font_import PRIVATE TOML_HEADER_ONLY=1 TOML_EXCEPTIONS=0
                                   ENGINE_SCHEMA_SOURCE_DIR="${PROJECT_SOURCE_DIR}/include/schemas/runtime")
    engine_apply_options(engine_font_import)
endfunction()

engine_register_plugin(NAME editor/font_import CONFIGURE engine_configure_font_import_plugin OPTIONS
                       ENGINE_ENABLE_ASSET_PIPELINE_DEPS VCPKG_FEATURES font-import)
