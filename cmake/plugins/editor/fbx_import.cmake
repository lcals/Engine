function(engine_configure_fbx_import_plugin)
    if(NOT EXISTS "${PROJECT_SOURCE_DIR}/third_party/ufbx/ufbx.c")
        message(FATAL_ERROR "FBX importer requires the pinned third_party/ufbx submodule")
    endif()
    add_library(engine_ufbx STATIC "${PROJECT_SOURCE_DIR}/third_party/ufbx/ufbx.c")
    target_include_directories(engine_ufbx SYSTEM PUBLIC "${PROJECT_SOURCE_DIR}/third_party/ufbx")
    set_target_properties(engine_ufbx PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES)
    add_library(engine_fbx_import STATIC src/engine/plugins/editor/fbx_import/fbx_importer.cpp
                                         src/engine/plugins/editor/fbx_import/fbx_document.cpp)
    add_library(engine::fbx_import ALIAS engine_fbx_import)
    target_include_directories(engine_fbx_import PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    get_target_property(_fbx_toml_includes tomlplusplus::tomlplusplus INTERFACE_INCLUDE_DIRECTORIES)
    target_include_directories(engine_fbx_import SYSTEM PRIVATE ${_fbx_toml_includes})
    target_link_libraries(
        engine_fbx_import
        PUBLIC engine::asset_pipeline
        PRIVATE engine::resource engine_ufbx)
    target_compile_definitions(
        engine_fbx_import
        PRIVATE TOML_HEADER_ONLY=1 TOML_EXCEPTIONS=0
                ENGINE_SCHEMA_SOURCE_DIR="${PROJECT_SOURCE_DIR}/include/schemas/runtime")
    engine_apply_options(engine_fbx_import)
endfunction()

engine_register_plugin(NAME editor/fbx_import CONFIGURE engine_configure_fbx_import_plugin OPTIONS
                       ENGINE_ENABLE_ASSET_PIPELINE_DEPS)
