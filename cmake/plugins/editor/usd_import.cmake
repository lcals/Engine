function(engine_configure_usd_import_plugin)
    add_library(engine_usd_import STATIC src/engine/plugins/editor/usd_import/usd_importer.cpp)
    add_library(engine::usd_import ALIAS engine_usd_import)
    target_link_libraries(
        engine_usd_import
        PUBLIC engine::asset_pipeline
        PRIVATE engine::resource engine_usd_sdk)
    target_include_directories(engine_usd_import PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    target_compile_definitions(
        engine_usd_import
        PRIVATE ENGINE_SCHEMA_SOURCE_DIR="${PROJECT_SOURCE_DIR}/include/schemas/runtime")
    get_target_property(_usd_toml_includes tomlplusplus::tomlplusplus INTERFACE_INCLUDE_DIRECTORIES)
    target_include_directories(engine_usd_import SYSTEM PRIVATE ${_usd_toml_includes})
    target_compile_definitions(engine_usd_import PRIVATE TOML_HEADER_ONLY=1 TOML_EXCEPTIONS=0)
    engine_apply_options(engine_usd_import)
endfunction()

engine_register_plugin(
    NAME
    editor/usd_import
    CONFIGURE
    engine_configure_usd_import_plugin
    OPTIONS
    ENGINE_ENABLE_RUNTIME_DEPS
    ENGINE_ENABLE_ASSET_PIPELINE_DEPS
    VCPKG_FEATURES
    usd-import)
