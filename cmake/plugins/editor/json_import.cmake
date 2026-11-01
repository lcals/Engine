function(engine_configure_json_import_plugin)
    add_library(engine_json_import STATIC src/engine/plugins/editor/json_import/json_importer.cpp)
    add_library(engine::json_import ALIAS engine_json_import)
    target_include_directories(engine_json_import PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    target_link_libraries(engine_json_import PUBLIC engine::asset_pipeline
                              PRIVATE nlohmann_json::nlohmann_json)
    target_compile_definitions(engine_json_import PRIVATE JSON_NOEXCEPTION=1)
    engine_apply_options(engine_json_import)
endfunction()

engine_register_plugin(NAME editor/json_import CONFIGURE engine_configure_json_import_plugin OPTIONS
                       ENGINE_ENABLE_ASSET_PIPELINE_DEPS)
