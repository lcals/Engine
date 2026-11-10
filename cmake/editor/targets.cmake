include("${PROJECT_SOURCE_DIR}/cmake/editor/schema.cmake")
engine_add_editor_schema()

if(ENGINE_ENABLE_EDITOR_DEPS OR ENGINE_ENABLE_ASSET_PIPELINE_DEPS
   OR "editor/asset_sync" IN_LIST ENGINE_SELECTED_PLUGINS)
    add_library(engine_editor_lock STATIC src/engine/editor/asset_pipeline/project_lock.cpp)
    target_include_directories(engine_editor_lock PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    target_link_libraries(engine_editor_lock PRIVATE engine::core)
    engine_apply_options(engine_editor_lock)
endif()

if(ENGINE_ENABLE_ASSET_PIPELINE_DEPS OR "editor/modeling" IN_LIST ENGINE_SELECTED_PLUGINS)
    add_library(engine_editor_mesh STATIC src/engine/editor/mesh/mesh_tangents.cpp)
    target_compile_features(engine_editor_mesh PRIVATE cxx_std_20)
    target_include_directories(engine_editor_mesh PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    target_link_libraries(engine_editor_mesh PRIVATE engine::scene_data mikktspace::mikktspace)
    engine_apply_options(engine_editor_mesh)
endif()

if(ENGINE_ENABLE_ASSET_PIPELINE_DEPS)
    target_sources(engine_editor_mesh PRIVATE src/engine/editor/mesh/mesh_lods.cpp)
    target_link_libraries(engine_editor_mesh
                          PRIVATE meshoptimizer::meshoptimizer engine::resource)
    get_target_property(_mesh_lod_toml_includes tomlplusplus::tomlplusplus
                        INTERFACE_INCLUDE_DIRECTORIES)
    target_include_directories(engine_editor_mesh SYSTEM PRIVATE ${_mesh_lod_toml_includes})
    target_compile_definitions(engine_editor_mesh
                               PRIVATE TOML_HEADER_ONLY=1 TOML_EXCEPTIONS=0)
endif()

if(ENGINE_ENABLE_EDITOR_DEPS)
    engine_add_system(editor project)
    target_link_libraries(engine_project PRIVATE engine::log)
    # 配置解析使用无异常头文件实现，避免与 vcpkg 的异常 ABI 混用。
    get_target_property(_editor_toml_includes tomlplusplus::tomlplusplus
                        INTERFACE_INCLUDE_DIRECTORIES)

    engine_add_system(editor editor src/engine/editor/editor_system.cpp)
    target_include_directories(engine_editor SYSTEM PRIVATE ${_editor_toml_includes})
    target_compile_definitions(engine_editor PRIVATE TOML_HEADER_ONLY=1 TOML_EXCEPTIONS=0)
    target_sources(
        engine_editor
        PRIVATE src/engine/editor/project/runtime_settings_io.cpp
                src/engine/editor/render/render_commands.cpp
                src/engine/editor/render/render_component_commands.cpp
                src/engine/editor/render/render_feature_commands.cpp
                src/engine/editor/scene/scene_preview.cpp
                src/engine/editor/editor_environment.cpp
                src/engine/editor/commands/editor_commands.cpp
                src/engine/editor/commands/scene_commands.cpp
                src/engine/editor/commands/scene_query.cpp
                src/engine/editor/commands/components/transform_commands.cpp
                src/engine/editor/commands/components/light_commands.cpp
                src/engine/editor/commands/components/camera_commands.cpp
                src/engine/editor/commands/components/material_commands.cpp
                src/engine/editor/commands/model_commands.cpp
                src/engine/editor/commands/components/component_edit.cpp
                src/engine/editor/commands/components/collider_commands.cpp
                src/engine/editor/commands/components/rigid_body_commands.cpp
                src/engine/editor/commands/components/audio_source_commands.cpp
                src/engine/editor/commands/components/animator_commands.cpp
                src/engine/editor/commands/components/character_motor_commands.cpp
                src/engine/editor/commands/components/audio_listener_commands.cpp)
    target_include_directories(engine_editor PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    target_link_libraries(
        engine_editor
        PUBLIC nlohmann_json::nlohmann_json
        PRIVATE engine::project
                engine::runtime
                engine::render
                engine::gameplay
                engine::app
                engine::log
                engine::resource
                engine::world
                engine::window
                engine::input
                engine_editor_lock
                imgui::imgui
                SDL3::SDL3)
    target_compile_definitions(engine_editor PUBLIC JSON_NOEXCEPTION=1)
    target_compile_definitions(
        engine_editor
        PRIVATE ENGINE_HAS_ASSET_PIPELINE=$<BOOL:${ENGINE_ENABLE_ASSET_PIPELINE_DEPS}>)

endif()

if(ENGINE_ENABLE_ASSET_PIPELINE_DEPS)
    engine_add_system(editor asset_pipeline)
    target_link_libraries(engine_asset_pipeline PRIVATE engine_editor_mesh)
    target_sources(
        engine_asset_pipeline
        PRIVATE src/engine/editor/asset_pipeline/asset_metadata.cpp
                src/engine/editor/asset_pipeline/asset_database.cpp
                src/engine/editor/asset_pipeline/asset_importer.cpp
                src/engine/editor/asset_pipeline/asset_importer_registry.cpp
                src/engine/editor/asset_pipeline/importer_utils.cpp
                src/engine/editor/asset_pipeline/editor_tools.cpp
                src/engine/editor/asset_pipeline/import_tool.cpp
                src/engine/editor/asset_pipeline/asset_io.cpp
                src/engine/editor/asset_pipeline/asset_publish.cpp
                src/engine/editor/asset_pipeline/scene_importer.cpp
                src/engine/editor/asset_pipeline/prefab_importer.cpp
                src/engine/editor/asset_pipeline/prefab_source.cpp
                src/engine/editor/asset_pipeline/prefab_composition.cpp
                src/engine/editor/asset_pipeline/components/scene_component_codec.cpp
                src/engine/editor/asset_pipeline/components/transform_codec.cpp
                src/engine/editor/asset_pipeline/components/collider_codec.cpp
                src/engine/editor/asset_pipeline/components/rigid_body_codec.cpp
                src/engine/editor/asset_pipeline/components/audio_source_codec.cpp
                src/engine/editor/asset_pipeline/components/ui_codec.cpp
                src/engine/editor/asset_pipeline/components/text_codec.cpp
                src/engine/editor/asset_pipeline/components/animator_codec.cpp
                src/engine/editor/asset_pipeline/components/bone_attachment_codec.cpp
                src/engine/editor/asset_pipeline/components/character_motor_codec.cpp
                src/engine/editor/asset_pipeline/components/audio_listener_codec.cpp
                src/engine/editor/asset_pipeline/components/camera_codec.cpp
                src/engine/editor/asset_pipeline/components/light_codec.cpp
                src/engine/editor/asset_pipeline/components/environment_codec.cpp
                src/engine/editor/asset_pipeline/components/post_process_codec.cpp
                src/engine/editor/asset_pipeline/components/planar_reflection_codec.cpp
                src/engine/editor/asset_pipeline/components/light_probe_volume_codec.cpp
                src/engine/editor/asset_pipeline/components/mesh_renderer_codec.cpp
                src/engine/editor/asset_pipeline/importers/mesh_importer.cpp
                src/engine/editor/asset_pipeline/importers/shader_importer.cpp
                src/engine/editor/asset_pipeline/importers/material_importer.cpp
                src/engine/editor/asset_pipeline/importers/scene_importer.cpp
                src/engine/editor/asset_pipeline/importers/prefab_importer.cpp
                src/engine/editor/asset_pipeline/importers/texture_importer.cpp
                src/engine/editor/asset_pipeline/importers/ktx_texture_importer.cpp
                src/engine/editor/asset_pipeline/importers/light_probe_importer.cpp
                src/engine/editor/asset_pipeline/importers/audio_importer.cpp
                src/engine/editor/asset_pipeline/importers/script_importer.cpp
                src/engine/editor/asset_pipeline/importers/source_importer.cpp)
    target_sources(engine_asset_pipeline PRIVATE src/engine/editor/asset_pipeline/animation_sampling.cpp)
    target_include_directories(engine_asset_pipeline PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    if(APPLE AND NOT IOS)
        target_sources(engine_asset_pipeline PRIVATE platforms/macos/editor/tools.cpp)
    else()
        target_sources(engine_asset_pipeline PRIVATE platforms/desktop/editor/tools.cpp)
    endif()
    target_link_libraries(
        engine_asset_pipeline
        PUBLIC engine::scene_data engine::task
        PRIVATE engine_editor_lock ozz_animation_offline ozz_animation
                engine::app
                engine::world
                engine::resource
                unofficial::sqlite3::sqlite3
                efsw::efsw
                basisu::basisu_encoder
                bgfx::bimg
                meshoptimizer::meshoptimizer
                nlohmann_json::nlohmann_json
                SDL3::SDL3)
    # vcpkg 的编译库使用异常 ABI；导入器按上游支持方式使用无异常头文件实现。
    get_target_property(_toml_includes tomlplusplus::tomlplusplus INTERFACE_INCLUDE_DIRECTORIES)
    target_include_directories(engine_asset_pipeline SYSTEM PRIVATE ${_toml_includes})
    target_compile_definitions(
        engine_asset_pipeline
        PRIVATE TOML_HEADER_ONLY=1
                TOML_EXCEPTIONS=0
                JSON_NOEXCEPTION=1
                ENGINE_SHADERC_EXECUTABLE="${ENGINE_SHADERC_EXECUTABLE}"
                ENGINE_FFMPEG_EXECUTABLE="${ENGINE_FFMPEG_EXECUTABLE}"
                ENGINE_FFPROBE_EXECUTABLE="${ENGINE_FFPROBE_EXECUTABLE}"
                ENGINE_BGFX_SHADER_INCLUDE_DIR="${ENGINE_BGFX_SHADER_INCLUDE_DIR}"
                ENGINE_SCHEMA_SOURCE_DIR="${PROJECT_SOURCE_DIR}/include/schemas")
    find_program(ENGINE_NODE_EXECUTABLE NAMES node)
    if(NOT ENGINE_NODE_EXECUTABLE)
        set(ENGINE_NODE_EXECUTABLE "")
    endif()
    target_compile_definitions(
        engine_asset_pipeline
        PRIVATE ENGINE_NODE_EXECUTABLE="${ENGINE_NODE_EXECUTABLE}"
                ENGINE_SCRIPT_BUILDER="${PROJECT_SOURCE_DIR}/scripts/build_scripts.mjs")

    if(TARGET engine_editor)
        target_sources(engine_editor PRIVATE src/engine/editor/scene/scene_persistence.cpp
                                             src/engine/editor/asset_pipeline/mesh_lod_commands.cpp
                                             src/engine/editor/commands/prefab_commands.cpp)
        target_link_libraries(engine_editor PRIVATE engine::asset_pipeline efsw::efsw)
    endif()

endif()
