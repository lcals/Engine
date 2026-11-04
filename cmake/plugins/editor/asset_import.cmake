function(engine_configure_asset_import_plugin)
    if(NOT EXISTS "${PROJECT_SOURCE_DIR}/third_party/ufbx/ufbx.c")
        message(FATAL_ERROR "Asset import requires the pinned third_party/ufbx submodule")
    endif()
    add_library(engine_ufbx STATIC "${PROJECT_SOURCE_DIR}/third_party/ufbx/ufbx.c")
    target_include_directories(engine_ufbx SYSTEM PUBLIC "${PROJECT_SOURCE_DIR}/third_party/ufbx")
    set_target_properties(engine_ufbx PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES)

    add_library(
        engine_asset_import STATIC
        src/engine/plugins/editor/asset_import/fbx_importer.cpp
        src/engine/plugins/editor/asset_import/fbx_document.cpp
        src/engine/plugins/editor/asset_import/fbx_authoring.cpp
        src/engine/plugins/editor/asset_import/fbx_motion.cpp
        src/engine/plugins/editor/asset_import/usd_importer.cpp
        src/engine/plugins/editor/asset_import/font_importer.cpp
        src/engine/plugins/editor/asset_import/ui_importer.cpp
        src/engine/plugins/editor/asset_import/json_importer.cpp)
    add_library(engine::asset_import ALIAS engine_asset_import)
    target_include_directories(engine_asset_import PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    get_target_property(_import_toml_includes tomlplusplus::tomlplusplus INTERFACE_INCLUDE_DIRECTORIES)
    target_include_directories(engine_asset_import SYSTEM PRIVATE ${_import_toml_includes})
    target_link_libraries(
        engine_asset_import
        PUBLIC engine::asset_pipeline
        PRIVATE engine::resource engine_ufbx ozz_animation_offline engine_editor_mesh
                engine_usd_sdk Freetype::Freetype nlohmann_json::nlohmann_json)
    target_compile_definitions(
        engine_asset_import
        PRIVATE TOML_HEADER_ONLY=1 TOML_EXCEPTIONS=0 JSON_NOEXCEPTION=1
                ENGINE_SCHEMA_SOURCE_DIR="${PROJECT_SOURCE_DIR}/include/schemas/runtime")
    engine_apply_options(engine_asset_import)
endfunction()

engine_register_plugin(NAME editor/asset_import CONFIGURE engine_configure_asset_import_plugin
    OPTIONS ENGINE_ENABLE_RUNTIME_DEPS ENGINE_ENABLE_ASSET_PIPELINE_DEPS
    VCPKG_FEATURES font-import usd-import)
