add_executable(engine_framework_test runtime/core/engine_test.cpp)
target_link_libraries(engine_framework_test PRIVATE engine::core)
engine_apply_options(engine_framework_test)
add_test(NAME engine.framework COMMAND engine_framework_test)

add_executable(engine_guid_map_test runtime/core/guid_map_test.cpp)
target_link_libraries(engine_guid_map_test PRIVATE engine::core)
engine_apply_options(engine_guid_map_test)
add_test(NAME engine.guid_map COMMAND engine_guid_map_test)

add_executable(engine_system_test runtime/core/system_test.cpp)
target_link_libraries(engine_system_test PRIVATE engine::core)
engine_apply_options(engine_system_test)
add_test(NAME engine.system COMMAND engine_system_test)

add_test(NAME engine.v8_cache
         COMMAND "${CMAKE_COMMAND}" "-DENGINE_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/v8-cache-test"
                 -P "${CMAKE_CURRENT_SOURCE_DIR}/build/v8_cache_test.cmake")
set_tests_properties(engine.v8_cache PROPERTIES TIMEOUT 30)

if(TARGET engine_task)
    add_executable(engine_task_test runtime/task/task_test.cpp)
    target_link_libraries(engine_task_test PRIVATE engine::task)
    engine_apply_options(engine_task_test)
    add_test(NAME runtime.task COMMAND engine_task_test)
    set_tests_properties(runtime.task PROPERTIES TIMEOUT 30)

    add_executable(engine_tbb_test runtime/task/tbb_test.cpp)
    target_compile_features(engine_tbb_test PRIVATE cxx_std_20)
    target_link_libraries(engine_tbb_test PRIVATE TBB::tbb)
    engine_apply_options(engine_tbb_test)
    add_test(NAME engine.tbb COMMAND engine_tbb_test)
    set_tests_properties(engine.tbb PROPERTIES TIMEOUT 60)
endif()

if(TARGET engine_platform)
    add_executable(engine_app_log_test runtime/app/app_log_test.cpp)
    target_link_libraries(engine_app_log_test PRIVATE engine::app engine::log Threads::Threads)
    engine_apply_options(engine_app_log_test)
    add_test(NAME engine.app_log COMMAND engine_app_log_test
                                         "${CMAKE_CURRENT_BINARY_DIR}/app-log-test-output")
    set_tests_properties(engine.app_log PROPERTIES TIMEOUT 30)
endif()

if(TARGET engine_storage)
    add_executable(engine_key_value_store_test runtime/storage/key_value_store_test.cpp)
    target_link_libraries(engine_key_value_store_test PRIVATE engine::storage)
    target_include_directories(engine_key_value_store_test
                               PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    engine_apply_options(engine_key_value_store_test)
    add_test(NAME engine.key_value_store COMMAND engine_key_value_store_test)
    set_tests_properties(engine.key_value_store PROPERTIES TIMEOUT 30)
endif()

if(TARGET engine_input)
    add_executable(engine_input_test runtime/input/input_test.cpp)
    target_link_libraries(engine_input_test PRIVATE engine::input engine::window SDL3::SDL3)
    engine_apply_options(engine_input_test)
    if(ENGINE_ENABLE_GPU_TESTS)
        add_test(NAME engine.input COMMAND engine_input_test)
        set_tests_properties(engine.input PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
    endif()
endif()

if(TARGET engine_resource)
    add_executable(engine_resource_read_test runtime/resource/resource_read_test.cpp)
    target_link_libraries(engine_resource_read_test PRIVATE engine::resource SDL3::SDL3)
    engine_apply_options(engine_resource_read_test)
    add_test(NAME engine.resource_read
             COMMAND engine_resource_read_test
                     "${CMAKE_CURRENT_BINARY_DIR}/resource-read-test-output")
    set_tests_properties(engine.resource_read PROPERTIES TIMEOUT 30)
endif()

if(TARGET engine_physics)
    add_executable(engine_physics_test runtime/physics/physics_test.cpp)
    target_link_libraries(engine_physics_test PRIVATE engine::physics engine::world)
    engine_apply_options(engine_physics_test)
    add_test(NAME engine.physics COMMAND engine_physics_test)
    set_tests_properties(engine.physics PROPERTIES TIMEOUT 60)
    add_executable(engine_physics_interaction_test runtime/physics/physics_interaction_test.cpp)
    target_link_libraries(engine_physics_interaction_test PRIVATE engine::physics engine::world)
    engine_apply_options(engine_physics_interaction_test)
    add_test(NAME engine.physics_interaction COMMAND engine_physics_interaction_test)
    set_tests_properties(engine.physics_interaction PROPERTIES TIMEOUT 60)
    add_executable(engine_character_motor_test runtime/physics/character_motor_test.cpp)
    target_link_libraries(engine_character_motor_test PRIVATE engine::physics engine::world)
    engine_apply_options(engine_character_motor_test)
    add_test(NAME engine.character_motor COMMAND engine_character_motor_test)
    set_tests_properties(engine.character_motor PROPERTIES TIMEOUT 60)
endif()

if(TARGET engine_audio)
    add_executable(engine_audio_test runtime/audio/audio_test.cpp)
    target_link_libraries(engine_audio_test PRIVATE engine::audio engine::world engine::resource
                                                    SDL3::SDL3)
    engine_apply_options(engine_audio_test)
    add_test(NAME engine.audio COMMAND engine_audio_test)
    set_tests_properties(engine.audio PROPERTIES TIMEOUT 60)
endif()

if(TARGET engine_ui_editor AND TARGET engine_font_import)
    add_executable(engine_ui_editor_test plugins/editor/ui/ui_editor_test.cpp)
    target_link_libraries(engine_ui_editor_test PRIVATE engine::ui_editor engine::font_import
        engine::render engine::font engine::input engine::world engine::resource SDL3::SDL3)
    target_include_directories(engine_ui_editor_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    engine_apply_options(engine_ui_editor_test)
    if(ENGINE_ENABLE_GPU_TESTS)
        add_test(NAME editor.ui_editor COMMAND engine_ui_editor_test
            "${PROJECT_SOURCE_DIR}/projects/showcase" "${CMAKE_CURRENT_BINARY_DIR}")
        set_tests_properties(editor.ui_editor PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
    endif()
endif()

if(TARGET engine_script)
    add_executable(engine_project_script_test plugins/runtime/script/project_script_test.cpp)
    target_link_libraries(engine_project_script_test PRIVATE engine::script)
    target_include_directories(engine_project_script_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    engine_apply_options(engine_project_script_test)
    engine_copy_script_data(engine_project_script_test)
    add_test(NAME engine.project_script COMMAND engine_project_script_test)
    set_tests_properties(engine.project_script PROPERTIES TIMEOUT 60)

    add_executable(engine_script_runtime_test plugins/runtime/script/script_runtime_test.cpp)
    target_link_libraries(
        engine_script_runtime_test
        PRIVATE engine::script
                engine::input
                engine::window
                SDL3::SDL3
                engine::app
                engine::log
                engine::resource
                engine::world
                engine::render
                nlohmann_json::nlohmann_json)
    target_compile_definitions(engine_script_runtime_test PRIVATE JSON_NOEXCEPTION=1)
    if(TARGET engine_effects)
        target_link_libraries(engine_script_runtime_test PRIVATE engine::effects)
        target_compile_definitions(engine_script_runtime_test PRIVATE ENGINE_TEST_EFFECTS_PLUGIN=1)
    endif()
    engine_apply_options(engine_script_runtime_test)
    engine_copy_script_data(engine_script_runtime_test)
    if(TARGET engine_resource_graph)
        target_link_libraries(engine_script_runtime_test PRIVATE engine::resource_graph)
        target_compile_definitions(engine_script_runtime_test PRIVATE ENGINE_TEST_RESOURCE_GRAPH=1)
    endif()
    add_test(NAME engine.script_runtime
             COMMAND engine_script_runtime_test
                     "${CMAKE_CURRENT_BINARY_DIR}/script-runtime-test-output")
    set_tests_properties(engine.script_runtime PROPERTIES TIMEOUT 60)

endif()

if(TARGET engine_asset_sync)
    add_executable(engine_asset_sync_test plugins/editor/asset_sync/asset_sync_test.cpp)
    target_link_libraries(engine_asset_sync_test PRIVATE engine::asset_sync engine::resource
                                                         nlohmann_json::nlohmann_json)
    target_compile_definitions(engine_asset_sync_test PRIVATE JSON_NOEXCEPTION=1)
    engine_apply_options(engine_asset_sync_test)
    add_test(NAME engine.asset_sync COMMAND engine_asset_sync_test
                                            "${CMAKE_CURRENT_BINARY_DIR}/asset-sync-test-output")
    set_tests_properties(engine.asset_sync PROPERTIES TIMEOUT 30)
endif()

if(TARGET mcp_app)
    set(_script_tooling_options "")
    if(NOT TARGET engine_modeling_mcp_tools)
        list(APPEND _script_tooling_options --without-modeling)
    endif()
    if(NOT TARGET engine_character_controller_mcp_tools)
        list(APPEND _script_tooling_options --without-character-controller)
    endif()
    if(NOT TARGET engine_scene_bake_mcp_tools)
        list(APPEND _script_tooling_options --without-scene-bake)
    endif()
    if(NOT TARGET engine_effects_mcp_tools)
        list(APPEND _script_tooling_options --without-effects)
    endif()
    if(NOT TARGET engine_terrain_mcp_tools)
        list(APPEND _script_tooling_options --without-terrain)
    endif()
    if(NOT TARGET engine_destruction_mcp_tools)
        list(APPEND _script_tooling_options --without-destruction)
    endif()
    if(NOT TARGET engine_ui_editor_mcp_tools)
        list(APPEND _script_tooling_options --without-ui)
    endif()
    find_package(
        Python3
        COMPONENTS Interpreter
        REQUIRED)
    add_test(NAME engine.android_asset_packaging
             COMMAND "${Python3_EXECUTABLE}"
                     "${CMAKE_CURRENT_SOURCE_DIR}/build/prepare_android_assets_test.py")
    set_tests_properties(engine.android_asset_packaging PROPERTIES TIMEOUT 60)
    add_test(NAME engine.desktop_export
             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/build/desktop_export_test.py")
    set_tests_properties(engine.desktop_export PROPERTIES TIMEOUT 60)
    add_test(NAME engine.android_mcp
             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/plugins/editor/mcp/android_mcp_test.py"
                     --mcp "$<TARGET_FILE:mcp_app>" --root "${PROJECT_SOURCE_DIR}")
    set_tests_properties(engine.android_mcp PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77)
    add_test(
        NAME engine.script_tooling
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/plugins/editor/mcp/script_tooling_test.py" --mcp
                "$<TARGET_FILE:mcp_app>" --root "${PROJECT_SOURCE_DIR}" ${_script_tooling_options})
    set_tests_properties(engine.script_tooling PROPERTIES TIMEOUT 120)
    add_test(NAME editor.project_script_compile
             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/plugins/editor/mcp/project_script_compile_test.py"
                     --root "${PROJECT_SOURCE_DIR}")
    set_tests_properties(editor.project_script_compile PROPERTIES TIMEOUT 60)
endif()

if(TARGET engine_mcp)
    add_executable(engine_mcp_registry_test plugins/editor/mcp/mcp_registry_test.cpp)
    target_link_libraries(engine_mcp_registry_test PRIVATE engine::mcp)
    if(TARGET engine::project_mcp_tools)
        target_link_libraries(engine_mcp_registry_test PRIVATE engine::project_mcp_tools)
        target_compile_definitions(engine_mcp_registry_test PRIVATE ENGINE_TEST_RENDER_TOOLS=1)
    endif()
    if(TARGET engine_scene_bake_mcp_tools)
        target_link_libraries(engine_mcp_registry_test PRIVATE engine::scene_bake_mcp_tools)
        target_compile_definitions(engine_mcp_registry_test PRIVATE ENGINE_TEST_SCENE_BAKE_TOOLS=1)
    endif()
    if(TARGET engine_terrain_mcp_tools)
        target_link_libraries(engine_mcp_registry_test PRIVATE engine::terrain_mcp_tools)
        target_compile_definitions(engine_mcp_registry_test PRIVATE ENGINE_TEST_TERRAIN_TOOLS=1)
    endif()
    if(TARGET engine_destruction_mcp_tools)
        target_link_libraries(engine_mcp_registry_test PRIVATE engine::destruction_mcp_tools)
        target_compile_definitions(engine_mcp_registry_test PRIVATE ENGINE_TEST_DESTRUCTION_TOOLS=1)
    endif()
    engine_apply_options(engine_mcp_registry_test)
    add_test(NAME engine.mcp_registry COMMAND engine_mcp_registry_test)
    set_tests_properties(engine.mcp_registry PROPERTIES TIMEOUT 30)
endif()

if(TARGET mcp_app)
    find_package(
        Python3
        COMPONENTS Interpreter
        REQUIRED)
    add_test(
        NAME editor.mcp_transport
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/plugins/editor/mcp/mcp_editor_transport_test.py"
                --mcp "$<TARGET_FILE:mcp_app>")
    set_tests_properties(editor.mcp_transport PROPERTIES TIMEOUT 60)
endif()

if(ENGINE_ENABLE_RUNTIME_DEPS
   OR ENGINE_ENABLE_EDITOR_DEPS
   OR ENGINE_ENABLE_ASSET_PIPELINE_DEPS
   OR ENGINE_ENABLE_V8)
    add_executable(engine_dependency_systems_test build/dependency_systems_test.cpp)
    target_link_libraries(engine_dependency_systems_test PRIVATE engine::core)
    target_compile_definitions(
        engine_dependency_systems_test
        PRIVATE ENGINE_TEST_PROJECT="${PROJECT_SOURCE_DIR}/projects/showcase"
                ENGINE_HAS_RUNTIME=$<BOOL:${ENGINE_ENABLE_RUNTIME_DEPS}>
                ENGINE_HAS_EDITOR=$<BOOL:${ENGINE_ENABLE_EDITOR_DEPS}>
                ENGINE_HAS_ASSET_PIPELINE=$<BOOL:${ENGINE_ENABLE_ASSET_PIPELINE_DEPS}>
                ENGINE_TEST_SCRIPT_PLUGIN=$<TARGET_EXISTS:engine_script>)
    if(ENGINE_ENABLE_RUNTIME_DEPS)
        target_link_libraries(engine_dependency_systems_test
                              PRIVATE ${ENGINE_RUNTIME_SYSTEM_TARGETS})
    endif()
    if(ENGINE_ENABLE_EDITOR_DEPS)
        target_link_libraries(engine_dependency_systems_test PRIVATE engine::editor engine::project)
    endif()
    if(ENGINE_ENABLE_ASSET_PIPELINE_DEPS)
        target_link_libraries(engine_dependency_systems_test PRIVATE engine::asset_pipeline)
    endif()
    if(TARGET engine_script)
        target_link_libraries(engine_dependency_systems_test PRIVATE engine::script)
    endif()
    engine_apply_options(engine_dependency_systems_test)
    add_test(NAME engine.dependencies COMMAND engine_dependency_systems_test)
    set_tests_properties(engine.dependencies PROPERTIES TIMEOUT 60)
endif()

if(TARGET engine_app)
    add_test(NAME engine.cli_help COMMAND engine_app --help)
    set_tests_properties(engine.cli_help PROPERTIES PASS_REGULAR_EXPRESSION "Usage: engine_app")
    add_test(NAME engine.cli_unknown_option COMMAND engine_app --project test)
    add_test(NAME engine.cli_invalid_frames COMMAND engine_app --frames invalid)
    set_tests_properties(engine.cli_unknown_option engine.cli_invalid_frames
                         PROPERTIES WILL_FAIL TRUE TIMEOUT 10)
endif()

if(TARGET engine_project)
    add_executable(engine_project_test editor/project/project_system_test.cpp)
    target_link_libraries(engine_project_test PRIVATE engine::project)
    engine_apply_options(engine_project_test)
    add_test(NAME engine.project COMMAND engine_project_test
                                         "${CMAKE_CURRENT_BINARY_DIR}/project-test-output")
endif()

if(TARGET engine_modeling)
    foreach(_part core geometry)
        add_executable(engine_modeling_${_part}_test
                       plugins/editor/modeling/modeling_${_part}_test.cpp)
        target_link_libraries(engine_modeling_${_part}_test PRIVATE engine::modeling)
        engine_apply_options(engine_modeling_${_part}_test)
        add_test(NAME modeling.${_part} COMMAND engine_modeling_${_part}_test)
        set_tests_properties(modeling.${_part} PROPERTIES TIMEOUT 60)
    endforeach()
endif()

if(TARGET engine_modeling_editor)
    add_executable(engine_modeling_commands_test plugins/editor/modeling/modeling_commands_test.cpp)
    target_link_libraries(
        engine_modeling_commands_test PRIVATE engine::modeling_editor engine::modeling
                                              engine::editor engine::world engine::resource)
    if(TARGET engine_modeling_mcp_tools)
        target_link_libraries(engine_modeling_commands_test PRIVATE engine::modeling_mcp_tools)
        target_compile_definitions(engine_modeling_commands_test PRIVATE ENGINE_TEST_MODELING_MCP=1)
    endif()
    engine_apply_options(engine_modeling_commands_test)
    add_test(NAME modeling.commands COMMAND engine_modeling_commands_test)
    set_tests_properties(modeling.commands PROPERTIES TIMEOUT 60)
endif()

if(TARGET engine_asset_pipeline AND TARGET engine_modeling_importer)
    add_executable(engine_modeling_pipeline_test plugins/editor/modeling/modeling_pipeline_test.cpp)
    target_link_libraries(
        engine_modeling_pipeline_test PRIVATE engine::modeling_importer engine::modeling
                                              engine::asset_pipeline engine::resource)
    engine_apply_options(engine_modeling_pipeline_test)
    add_test(NAME modeling.pipeline COMMAND engine_modeling_pipeline_test)
    set_tests_properties(modeling.pipeline PROPERTIES TIMEOUT 120)
endif()

if(ENGINE_ENABLE_GPU_TESTS
   AND TARGET editor_app
   AND TARGET mcp_app
   AND TARGET engine_script
   AND TARGET engine_modeling_mcp_tools)
    add_test(
        NAME modeling.mcp
        COMMAND
            "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/plugins/editor/modeling/mcp_modeling_test.py" --editor
            "$<TARGET_FILE:editor_app>" --mcp "$<TARGET_FILE:mcp_app>" --root
            "${PROJECT_SOURCE_DIR}")
    set_tests_properties(modeling.mcp PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE)
    add_test(
        NAME editor.physics_audio_mcp
        COMMAND
            "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/plugins/editor/mcp/mcp_physics_audio_test.py"
            --editor "$<TARGET_FILE:editor_app>" --mcp "$<TARGET_FILE:mcp_app>" --root
            "${PROJECT_SOURCE_DIR}")
    set_tests_properties(editor.physics_audio_mcp PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE
                                                             ENVIRONMENT "SDL_AUDIODRIVER=dummy")
endif()

if(TARGET engine_editor)
    add_executable(engine_physics_audio_commands_test
                   editor/commands/physics_audio_commands_test.cpp)
    target_link_libraries(engine_physics_audio_commands_test PRIVATE engine::editor engine::world
                                                                     engine::resource)
    engine_apply_options(engine_physics_audio_commands_test)
    add_test(NAME editor.physics_audio_commands COMMAND engine_physics_audio_commands_test)
    set_tests_properties(editor.physics_audio_commands PROPERTIES TIMEOUT 30)
    add_executable(engine_editor_commands_test editor/commands/editor_commands_test.cpp)
    target_include_directories(engine_editor_commands_test
                               PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    target_link_libraries(engine_editor_commands_test PRIVATE engine::editor engine::world
                                                              engine::resource)
    engine_apply_options(engine_editor_commands_test)
    add_test(NAME editor.commands COMMAND engine_editor_commands_test)
    set_tests_properties(editor.commands PROPERTIES TIMEOUT 30)
    add_executable(engine_editor_environment_test editor/host/editor_environment_test.cpp)
    target_link_libraries(engine_editor_environment_test PRIVATE engine::editor engine::project
                                                                 engine::app engine::resource)
    engine_apply_options(engine_editor_environment_test)
    add_test(NAME engine.editor_environment COMMAND engine_editor_environment_test
                                                    "${CMAKE_CURRENT_BINARY_DIR}")
    add_executable(engine_editor_test editor/host/editor_system_test.cpp)
    target_link_libraries(engine_editor_test PRIVATE engine::editor engine::project imgui::imgui)
    engine_apply_options(engine_editor_test)
    add_test(NAME engine.editor COMMAND engine_editor_test
                                        "${PROJECT_SOURCE_DIR}/projects/showcase")
endif()

if(TARGET editor_app)
    add_test(NAME editor.cli_help COMMAND editor_app --help)
    set_tests_properties(editor.cli_help PROPERTIES PASS_REGULAR_EXPRESSION "Usage: editor_app")
    add_test(NAME editor.cli_missing_project COMMAND editor_app)
    add_test(NAME editor.cli_invalid_project
             COMMAND editor_app --project "${CMAKE_CURRENT_BINARY_DIR}/missing-project" --frames 1)
    add_test(NAME editor.cli_invalid_frames
             COMMAND editor_app --project "${PROJECT_SOURCE_DIR}/projects/showcase" --frames
                     invalid)
    set_tests_properties(editor.cli_missing_project editor.cli_invalid_project
                         editor.cli_invalid_frames PROPERTIES WILL_FAIL TRUE TIMEOUT 10)
endif()

if(ENGINE_ENABLE_RUNTIME_DEPS)
    add_executable(engine_planar_reflection_test runtime/render/planar_reflection_test.cpp)
    target_include_directories(engine_planar_reflection_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    target_link_libraries(engine_planar_reflection_test PRIVATE engine::render bgfx::bx)
    engine_apply_options(engine_planar_reflection_test)
    add_test(NAME engine.planar_reflection COMMAND engine_planar_reflection_test)
    add_executable(engine_render_foundation_test runtime/render/render_foundation_test.cpp)
    target_link_libraries(engine_render_foundation_test PRIVATE engine::world engine::resource
                                                                engine::render)
    if(TARGET engine_resource_graph)
        target_link_libraries(engine_render_foundation_test PRIVATE engine::resource_graph)
        target_compile_definitions(engine_render_foundation_test
                                   PRIVATE ENGINE_TEST_RESOURCE_GRAPH=1)
    endif()
    engine_apply_options(engine_render_foundation_test)
    add_test(NAME engine.render_foundation COMMAND engine_render_foundation_test)
    set_tests_properties(engine.render_foundation PROPERTIES TIMEOUT 30)
    add_executable(engine_window_render_test runtime/render/window_render_test.cpp)
    target_link_libraries(
        engine_window_render_test
        PRIVATE engine::runtime
                engine::window
                engine::render
                engine::world
                engine::resource
                SDL3::SDL3
                bgfx::bgfx
                bgfx::bimg
                basisu::basisu_encoder)
    engine_apply_options(engine_window_render_test)
    if(TARGET engine_resource_graph)
        target_link_libraries(engine_window_render_test PRIVATE engine::resource_graph)
        target_compile_definitions(engine_window_render_test PRIVATE ENGINE_TEST_RESOURCE_GRAPH=1)
    endif()
    if(ENGINE_ENABLE_GPU_TESTS)
        add_test(NAME engine.window_render COMMAND engine_window_render_test)
        set_tests_properties(engine.window_render PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
    endif()
endif()

if(ENGINE_ENABLE_ASSET_PIPELINE_DEPS)
    add_executable(engine_audio_pipeline_test editor/asset_pipeline/audio_pipeline_test.cpp)
    target_link_libraries(engine_audio_pipeline_test PRIVATE engine::asset_pipeline
                                                             engine::resource)
    engine_apply_options(engine_audio_pipeline_test)
    add_test(NAME editor.audio_pipeline COMMAND engine_audio_pipeline_test)
    set_tests_properties(editor.audio_pipeline PROPERTIES TIMEOUT 120)
    add_executable(engine_texture_pipeline_test editor/asset_pipeline/texture_pipeline_test.cpp)
    target_link_libraries(
        engine_texture_pipeline_test PRIVATE engine::asset_pipeline engine::resource engine::world
                                             engine::scene_data engine::render basisu::basisu_encoder
                                             bgfx::bimg)
    target_include_directories(engine_texture_pipeline_test
                               PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    target_compile_definitions(engine_texture_pipeline_test
                               PRIVATE ENGINE_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    engine_apply_options(engine_texture_pipeline_test)
    add_test(NAME editor.texture_pipeline COMMAND engine_texture_pipeline_test)
    set_tests_properties(editor.texture_pipeline PROPERTIES TIMEOUT 120)
    add_executable(engine_prefab_fields_test editor/asset_pipeline/prefab_fields_test.cpp)
    target_link_libraries(engine_prefab_fields_test PRIVATE engine::asset_pipeline)
    engine_apply_options(engine_prefab_fields_test)
    add_test(NAME editor.prefab_fields COMMAND engine_prefab_fields_test)

    add_executable(engine_prefab_pipeline_test editor/asset_pipeline/prefab_pipeline_test.cpp)
    target_include_directories(engine_prefab_pipeline_test
                               PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    target_link_libraries(engine_prefab_pipeline_test PRIVATE engine::asset_pipeline
                                                             engine::resource engine::world)
    engine_apply_options(engine_prefab_pipeline_test)
    add_test(NAME editor.prefab_pipeline COMMAND engine_prefab_pipeline_test)

    add_executable(engine_asset_pipeline_test editor/asset_pipeline/asset_pipeline_test.cpp)
    target_include_directories(engine_asset_pipeline_test
                               PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    target_link_libraries(engine_asset_pipeline_test PRIVATE engine::asset_pipeline
                                                             engine::resource engine::scene_data)
    engine_apply_options(engine_asset_pipeline_test)
    add_test(NAME editor.asset_pipeline COMMAND engine_asset_pipeline_test)
    set_tests_properties(editor.asset_pipeline PROPERTIES TIMEOUT 90)

    if(TARGET engine_asset_build)
        add_executable(engine_asset_build_test plugins/editor/asset_build/asset_build_test.cpp)
        target_link_libraries(engine_asset_build_test
                              PRIVATE engine::asset_build engine::asset_pipeline engine::resource)
        engine_apply_options(engine_asset_build_test)
        add_test(NAME editor.asset_build COMMAND engine_asset_build_test)
        set_tests_properties(editor.asset_build PROPERTIES TIMEOUT 90)
    endif()

    if(TARGET asset_build_app AND ENGINE_NODE_EXECUTABLE)
        add_test(
            NAME editor.showcase_navigation
            COMMAND
                "${ENGINE_NODE_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/projects/showcase/tests/launcher_test.mjs"
                "${PROJECT_SOURCE_DIR}")
        set_tests_properties(editor.showcase_navigation PROPERTIES TIMEOUT 60)
        add_test(
            NAME editor.showcase_navigation_crowd
            COMMAND "${ENGINE_NODE_EXECUTABLE}"
                    "${PROJECT_SOURCE_DIR}/projects/showcase/tests/navigation_crowd_test.mjs"
                    "${PROJECT_SOURCE_DIR}")
        set_tests_properties(editor.showcase_navigation_crowd PROPERTIES TIMEOUT 60)
        add_test(
            NAME editor.showcase_physics_interactions
            COMMAND "${ENGINE_NODE_EXECUTABLE}"
                    "${PROJECT_SOURCE_DIR}/projects/showcase/tests/physics_interactions_test.mjs"
                    "${PROJECT_SOURCE_DIR}")
        set_tests_properties(editor.showcase_physics_interactions PROPERTIES TIMEOUT 60)
        add_test(
            NAME editor.showcase_contract
            COMMAND "${ENGINE_NODE_EXECUTABLE}"
                    "${PROJECT_SOURCE_DIR}/projects/showcase/tests/case_contract_test.mjs"
                    "${PROJECT_SOURCE_DIR}")
        set_tests_properties(editor.showcase_contract PROPERTIES TIMEOUT 60)
        add_test(
            NAME editor.showcase_persistent_ui
            COMMAND "${ENGINE_NODE_EXECUTABLE}"
                    "${PROJECT_SOURCE_DIR}/projects/showcase/tests/persistent_ui_test.mjs"
                    "${PROJECT_SOURCE_DIR}")
        set_tests_properties(editor.showcase_persistent_ui PROPERTIES TIMEOUT 60)
        find_package(
            Python3
            COMPONENTS Interpreter
            REQUIRED)
        add_test(
            NAME editor.asset_pipeline_scripts
            COMMAND
                "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/editor/asset_pipeline/asset_pipeline_script_test.py"
                --builder "$<TARGET_FILE:asset_build_app>" --project
                "${PROJECT_SOURCE_DIR}/projects/showcase")
        set_tests_properties(editor.asset_pipeline_scripts PROPERTIES TIMEOUT 180)
        add_test(NAME editor.portable_tools
                 COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/editor/asset_pipeline/editor_tools_test.py"
                         --root "${PROJECT_SOURCE_DIR}" --node "${ENGINE_NODE_EXECUTABLE}")
        set_tests_properties(editor.portable_tools PROPERTIES TIMEOUT 90)
    endif()

    if(TARGET usd)
        set_tests_properties(
            engine.dependencies PROPERTIES ENVIRONMENT
                                           "PXR_PLUGINPATH_NAME=$<TARGET_FILE_DIR:usd>/usd")
    endif()

    add_test(
        NAME engine.asset_tools
        COMMAND
            "${CMAKE_COMMAND}" "-DENGINE_SHADERC=${ENGINE_BGFX_SHADERC_EXECUTABLE}"
            "-DENGINE_TEXTUREC=${ENGINE_BGFX_TEXTUREC_EXECUTABLE}"
            "-DENGINE_GEOMETRYC=${ENGINE_BGFX_GEOMETRYC_EXECUTABLE}"
            "-DENGINE_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/asset-tools-output" -P
            "${CMAKE_CURRENT_SOURCE_DIR}/editor/asset_pipeline/asset_tools_test.cmake")
    set_tests_properties(engine.asset_tools PROPERTIES TIMEOUT 100)

    if(TARGET engine_usd_import)
        add_executable(engine_usd_test plugins/editor/usd_import/usd_test.cpp)
        target_compile_features(engine_usd_test PRIVATE cxx_std_20)
        target_link_libraries(engine_usd_test PRIVATE engine::core engine_usd_sdk)
        engine_apply_options(engine_usd_test)
        engine_copy_usd_data(engine_usd_test)
        add_test(NAME engine.usd COMMAND engine_usd_test
                                         "${CMAKE_CURRENT_BINARY_DIR}/usd-test-output")
        set_tests_properties(engine.usd PROPERTIES TIMEOUT 60 ENVIRONMENT
                                                   "PXR_PLUGINPATH_NAME=$<TARGET_FILE_DIR:usd>/usd")
    endif()
endif()

if(ENGINE_ENABLE_V8)
    add_executable(engine_v8_test
                   plugins/runtime/script/v8_test.cpp plugins/runtime/script/v8_smoke.cpp)
    target_link_libraries(engine_v8_test PRIVATE engine::v8)
    engine_apply_options(engine_v8_test)
    if(ENGINE_V8_ICU_DATA)
        add_custom_command(
            TARGET engine_v8_test
            POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${ENGINE_V8_ICU_DATA}"
                    "$<TARGET_FILE_DIR:engine_v8_test>/icudtl.dat"
            VERBATIM)
    endif()
    add_test(NAME engine.v8 COMMAND engine_v8_test)
    set_tests_properties(engine.v8 PROPERTIES TIMEOUT 60)
endif()

if(TARGET engine_world)
    add_executable(engine_resource_catalog_test runtime/resource/resource_catalog_test.cpp)
    target_link_libraries(engine_resource_catalog_test PRIVATE engine::world engine::resource)
    engine_apply_options(engine_resource_catalog_test)
    add_test(NAME engine.resource_catalog
             COMMAND engine_resource_catalog_test
                     "${CMAKE_CURRENT_BINARY_DIR}/resource-catalog-test-output")
    set_tests_properties(engine.resource_catalog PROPERTIES TIMEOUT 30)
    add_executable(engine_scene_test runtime/scene/scene_test.cpp)
    target_link_libraries(engine_scene_test PRIVATE engine::world)
    engine_apply_options(engine_scene_test)
    add_test(NAME engine.scene COMMAND engine_scene_test)
    set_tests_properties(engine.scene PROPERTIES TIMEOUT 30)
    add_executable(engine_ecs_scene_test runtime/scene/ecs_scene_test.cpp)
    target_link_libraries(engine_ecs_scene_test PRIVATE engine::world engine::scene_ecs)
    engine_apply_options(engine_ecs_scene_test)
    add_test(NAME engine.ecs_scene COMMAND engine_ecs_scene_test)
    set_tests_properties(engine.ecs_scene PROPERTIES TIMEOUT 30)

    add_executable(engine_prefab_test runtime/scene/prefab_test.cpp)
    target_link_libraries(engine_prefab_test PRIVATE engine::world engine::resource)
    if(TARGET engine_character_controller)
        target_link_libraries(engine_prefab_test PRIVATE engine::character_controller)
        target_compile_definitions(engine_prefab_test PRIVATE ENGINE_TEST_CHARACTER_CONTROLLER=1)
    endif()
    engine_apply_options(engine_prefab_test)
    add_test(NAME engine.prefab COMMAND engine_prefab_test)

    add_executable(engine_scene_data_test runtime/scene/scene_data_test.cpp)
    target_link_libraries(engine_scene_data_test PRIVATE engine::world engine::resource)
    engine_apply_options(engine_scene_data_test)
    add_test(NAME engine.scene_data COMMAND engine_scene_data_test)
    if(ENGINE_ENABLE_ASSET_PIPELINE_DEPS AND ENGINE_NODE_EXECUTABLE)
        find_package(
            Python3
            COMPONENTS Interpreter
            REQUIRED)
        add_test(
            NAME engine.scene_schema_cross_language
            COMMAND
                "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/runtime/scene/scene_schema_test.py" --binary
                "$<TARGET_FILE:engine_scene_data_test>" --generated
                "${PROJECT_BINARY_DIR}/generated/typescript" --settings
                "${PROJECT_SOURCE_DIR}/projects/showcase/settings" --work-dir
                "${CMAKE_CURRENT_BINARY_DIR}" --node "${ENGINE_NODE_EXECUTABLE}")
        set_tests_properties(engine.scene_schema_cross_language PROPERTIES TIMEOUT 180)
    endif()
endif()

if(TARGET editor_app
   AND TARGET mcp_app
   AND TARGET engine_script
   AND ENGINE_ENABLE_GPU_TESTS)
    add_test(
        NAME editor.project_lifecycle
        COMMAND
            "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/editor/host/editor_lifecycle_test.py" --editor
            "$<TARGET_FILE:editor_app>" --mcp "$<TARGET_FILE:mcp_app>" --root
            "${PROJECT_SOURCE_DIR}")
    set_tests_properties(editor.project_lifecycle PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
    add_test(
        NAME editor.mcp_commands
        COMMAND
            "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/plugins/editor/mcp/mcp_editor_test.py" --editor
            "$<TARGET_FILE:editor_app>" --mcp "$<TARGET_FILE:mcp_app>" --root
            "${PROJECT_SOURCE_DIR}")
    set_tests_properties(editor.mcp_commands PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
endif()

if(TARGET editor_app
   AND TARGET mcp_app
   AND TARGET engine_asset_pipeline
   AND NOT TARGET engine_script
   AND ENGINE_ENABLE_GPU_TESTS)
    add_test(
        NAME editor.without_script
        COMMAND
            "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/editor/host/editor_without_script_test.py"
            --editor "$<TARGET_FILE:editor_app>" --mcp "$<TARGET_FILE:mcp_app>" --root
            "${PROJECT_SOURCE_DIR}")
    set_tests_properties(editor.without_script PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
endif()

if(TARGET engine_asset_pipeline)
    add_executable(engine_asset_metadata_test editor/asset_pipeline/asset_metadata_test.cpp)
    target_include_directories(engine_asset_metadata_test
                               PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    target_link_libraries(engine_asset_metadata_test PRIVATE engine::asset_pipeline
                                                             engine::resource)
    engine_apply_options(engine_asset_metadata_test)
    add_test(NAME editor.asset_metadata COMMAND engine_asset_metadata_test)
endif()

if(TARGET engine_modeling_editor AND TARGET engine_modeling_importer)
    add_executable(engine_scene_persistence_test editor/scene/scene_persistence_test.cpp)
    target_include_directories(engine_scene_persistence_test
                               PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    target_link_libraries(
        engine_scene_persistence_test
        PRIVATE engine::modeling_editor engine::modeling_importer engine::editor
                engine::asset_pipeline engine::world engine::resource)
    engine_apply_options(engine_scene_persistence_test)
    add_test(NAME editor.scene_persistence COMMAND engine_scene_persistence_test)
endif()

if(TARGET engine_resource_graph)
    add_executable(engine_resource_graph_test
                   plugins/runtime/resource_graph/resource_graph_test.cpp)
    target_link_libraries(engine_resource_graph_test PRIVATE engine::resource_graph
                                                             engine::resource)
    engine_apply_options(engine_resource_graph_test)
    add_test(NAME plugins.runtime.resource_graph COMMAND engine_resource_graph_test)
    set_tests_properties(plugins.runtime.resource_graph PROPERTIES TIMEOUT 60)
endif()

add_test(
    NAME engine.plugin_selection
    COMMAND
        "${CMAKE_COMMAND}" "-DENGINE_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "-DENGINE_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/plugin-selection-test" -P
        "${CMAKE_CURRENT_SOURCE_DIR}/build/plugin_selection_test.cmake")
set_tests_properties(engine.plugin_selection PROPERTIES TIMEOUT 30)

add_test(
    NAME engine.architecture_boundaries
    COMMAND
        "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/check_architecture.py" --root
        "${PROJECT_SOURCE_DIR}" --targets "${PROJECT_BINARY_DIR}/architecture" --config "$<CONFIG>")
add_test(NAME engine.architecture_rules
         COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/build/architecture_rules_test.py")

if(TARGET engine_world)
    add_executable(engine_component_registry_test runtime/scene/component_registry_test.cpp)
    target_link_libraries(engine_component_registry_test PRIVATE engine::world engine::resource engine::scene_ecs)
    engine_apply_options(engine_component_registry_test)
    add_test(NAME engine.component_registry COMMAND engine_component_registry_test)
endif()

if(TARGET engine_resource)
    add_executable(engine_resource_codecs_test runtime/resource/resource_codecs_test.cpp)
    target_link_libraries(engine_resource_codecs_test PRIVATE engine::resource)
    engine_apply_options(engine_resource_codecs_test)
    add_test(NAME resource.codecs COMMAND engine_resource_codecs_test)
endif()

if(TARGET engine_asset_pipeline)
    add_executable(engine_asset_importer_registry_test
                   editor/asset_pipeline/asset_importer_registry_test.cpp)
    target_link_libraries(engine_asset_importer_registry_test PRIVATE engine::asset_pipeline)
    target_include_directories(engine_asset_importer_registry_test
                               PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    engine_apply_options(engine_asset_importer_registry_test)
    add_test(NAME editor.asset_importer_registry COMMAND engine_asset_importer_registry_test)
endif()

if(TARGET engine_animation)
    add_executable(engine_animation_test runtime/animation/animation_test.cpp)
    target_link_libraries(engine_animation_test PRIVATE engine::animation engine::world
                                                        engine::resource)
    engine_apply_options(engine_animation_test)
    add_test(NAME engine.animation COMMAND engine_animation_test)
endif()
if(TARGET engine_fbx_import)
    add_executable(engine_fbx_pipeline_test plugins/editor/fbx_import/fbx_pipeline_test.cpp)
    target_link_libraries(engine_fbx_pipeline_test PRIVATE engine::fbx_import engine::resource)
    if(TARGET engine_asset_build)
        target_link_libraries(engine_fbx_pipeline_test PRIVATE engine::asset_build)
        target_compile_definitions(engine_fbx_pipeline_test PRIVATE ENGINE_FBX_TEST_BUILD=1)
    endif()
    if(TARGET engine_resource_graph)
        target_link_libraries(engine_fbx_pipeline_test PRIVATE engine::resource_graph)
        target_compile_definitions(engine_fbx_pipeline_test PRIVATE ENGINE_FBX_TEST_GRAPH=1)
    endif()
    target_compile_definitions(
        engine_fbx_pipeline_test
        PRIVATE ENGINE_FBX_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/projects/showcase/assets/models")
    engine_apply_options(engine_fbx_pipeline_test)
    add_test(NAME editor.fbx_pipeline COMMAND engine_fbx_pipeline_test)

    add_executable(engine_fbx_bind_pose_test plugins/editor/fbx_import/fbx_bind_pose_test.cpp)
    target_link_libraries(engine_fbx_bind_pose_test PRIVATE engine::fbx_import engine_ufbx)
    target_include_directories(engine_fbx_bind_pose_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    target_compile_definitions(
        engine_fbx_bind_pose_test
        PRIVATE ENGINE_FBX_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/projects/showcase/assets/models"
                ENGINE_UFBX_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/third_party/ufbx/data")
    engine_apply_options(engine_fbx_bind_pose_test)
    add_test(NAME editor.fbx_bind_pose COMMAND engine_fbx_bind_pose_test)
endif()

if(TARGET engine_font_import)
    add_executable(engine_font_pipeline_test plugins/editor/font_import/font_pipeline_test.cpp)
    target_link_libraries(engine_font_pipeline_test PRIVATE engine::font_import engine::resource)
    engine_apply_options(engine_font_pipeline_test)
    add_test(NAME editor.font_pipeline
             COMMAND engine_font_pipeline_test
                     "${PROJECT_SOURCE_DIR}/projects/showcase/assets/fonts/lato-regular.ttf")
    set_tests_properties(editor.font_pipeline PROPERTIES TIMEOUT 120)
endif()

if(TARGET engine_json_import)
    add_executable(engine_json_importer_test plugins/editor/json_import/json_importer_test.cpp)
    target_link_libraries(engine_json_importer_test PRIVATE engine::json_import engine::resource)
    engine_apply_options(engine_json_importer_test)
    add_test(NAME editor.json_importer COMMAND engine_json_importer_test)
endif()

if(TARGET engine_ui_import AND TARGET engine_font_import)
    add_executable(engine_ui_pipeline_test plugins/editor/ui_import/ui_pipeline_test.cpp)
    target_link_libraries(engine_ui_pipeline_test PRIVATE engine::ui_import engine::font_import
                                                          engine::resource bgfx::bimg)
    engine_apply_options(engine_ui_pipeline_test)
    add_test(NAME editor.ui_pipeline
             COMMAND engine_ui_pipeline_test
                     "${PROJECT_SOURCE_DIR}/projects/showcase/assets/fonts/lato-regular.ttf")
    set_tests_properties(editor.ui_pipeline PROPERTIES TIMEOUT 120)
endif()

if(TARGET engine_ui_import
   AND TARGET engine_font_import
   AND TARGET engine_ui
   AND TARGET engine_font
   AND TARGET engine_render
   AND TARGET engine_window
   AND TARGET engine_world)
    add_executable(engine_font_ui_render_test runtime/ui/font_ui_render_test.cpp)
    target_link_libraries(
        engine_font_ui_render_test
        PRIVATE engine::ui_import
                engine::font_import
                engine::resource
                engine::world
                engine::window
                engine::render
                engine::font
                engine::ui
                SDL3::SDL3
                bgfx::bgfx)
    target_compile_definitions(
        engine_font_ui_render_test
        PRIVATE
            ENGINE_FONT_TEST_SHADER_DIR="${PROJECT_SOURCE_DIR}/src/engine/runtime/render/shaders")
    engine_apply_options(engine_font_ui_render_test)
    if(ENGINE_ENABLE_GPU_TESTS)
        add_test(NAME engine.font_ui_render COMMAND engine_font_ui_render_test
                                                    "${PROJECT_SOURCE_DIR}/projects/showcase")
        set_tests_properties(engine.font_ui_render PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
    endif()
endif()

if(TARGET engine_editor AND TARGET engine_animation)
    add_executable(engine_animation_commands_test editor/commands/animation_commands_test.cpp)
    target_link_libraries(engine_animation_commands_test PRIVATE engine::editor engine::animation
                                                                 engine::world engine::resource)
    engine_apply_options(engine_animation_commands_test)
    add_test(NAME editor.animation_commands COMMAND engine_animation_commands_test)
endif()

if(TARGET engine_character_controller)
    add_executable(engine_character_controller_test
                   plugins/runtime/character_controller/character_controller_test.cpp)
    target_link_libraries(engine_character_controller_test PRIVATE engine::character_controller)
    engine_apply_options(engine_character_controller_test)
    add_test(NAME engine.character_controller COMMAND engine_character_controller_test)
    if(TARGET engine_runtime)
        add_executable(engine_character_host_test
                       plugins/runtime/character_controller/character_host_test.cpp)
        target_link_libraries(
            engine_character_host_test PRIVATE engine::runtime engine::character_controller
                                               engine::resource engine::render SDL3::SDL3)
        target_include_directories(engine_character_host_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
        target_compile_definitions(engine_character_host_test
                                   PRIVATE ENGINE_HAS_CHARACTER_CONTROLLER=1)
        engine_apply_options(engine_character_host_test)
        if(ENGINE_ENABLE_GPU_TESTS)
            add_test(NAME engine.character_host COMMAND engine_character_host_test)
            set_tests_properties(engine.character_host PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
        endif()
    endif()
endif()

if(TARGET engine_usd_import AND TARGET engine_fbx_import)
    add_executable(engine_usd_pipeline_test plugins/editor/usd_import/usd_pipeline_test.cpp)
    target_link_libraries(engine_usd_pipeline_test PRIVATE engine::usd_import engine::fbx_import
                                                           engine::resource)
    target_compile_definitions(
        engine_usd_pipeline_test
        PRIVATE ENGINE_FBX_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/projects/showcase/assets/models")
    engine_apply_options(engine_usd_pipeline_test)
    engine_copy_usd_data(engine_usd_pipeline_test)
    add_test(NAME editor.usd_pipeline COMMAND engine_usd_pipeline_test)
endif()
if(TARGET engine_animation)
    add_executable(engine_model_resource_test runtime/resource/model_resource_test.cpp)
    target_link_libraries(engine_model_resource_test PRIVATE engine::world engine::resource
                                                             engine::animation)
    engine_apply_options(engine_model_resource_test)
    add_test(NAME engine.model_resource COMMAND engine_model_resource_test)
endif()
if(TARGET engine_script
   AND TARGET engine_ui_import
   AND TARGET engine_font_import
   AND TARGET engine_ui
   AND TARGET engine_font
   AND TARGET engine_render)
    add_executable(engine_ui_script_test plugins/runtime/script/ui_script_test.cpp)
    target_link_libraries(
        engine_ui_script_test
        PRIVATE engine::script
                engine::app
                engine::log
                engine::ui_import
                engine::font_import
                engine::resource
                engine::world
                engine::window
                engine::render
                engine::font
                engine::ui
                SDL3::SDL3
                bgfx::bgfx)
    engine_apply_options(engine_ui_script_test)
    engine_copy_script_data(engine_ui_script_test)
    if(ENGINE_ENABLE_GPU_TESTS)
        add_test(NAME engine.ui_script COMMAND engine_ui_script_test
                                               "${PROJECT_SOURCE_DIR}/projects/showcase")
        set_tests_properties(engine.ui_script PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
    endif()
endif()

if(TARGET engine_editor AND TARGET engine_animation)
    add_executable(engine_model_commands_test editor/commands/model_commands_test.cpp)
    target_link_libraries(engine_model_commands_test PRIVATE engine::editor engine::world
                                                             engine::resource engine::animation)
    engine_apply_options(engine_model_commands_test)
    add_test(NAME editor.model_commands COMMAND engine_model_commands_test)
endif()

if(TARGET engine_character_controller_editor AND TARGET engine_editor)
    add_executable(engine_character_controller_editor_test
                   plugins/editor/character_controller/character_controller_editor_test.cpp)
    target_link_libraries(engine_character_controller_editor_test
                          PRIVATE engine::character_controller_editor engine::editor)
    engine_apply_options(engine_character_controller_editor_test)
    add_test(NAME editor.character_controller COMMAND engine_character_controller_editor_test)
endif()
if(TARGET engine_script AND TARGET engine_animation)
    add_executable(engine_model_animation_script_test
                   plugins/runtime/script/model_animation_script_test.cpp)
    target_link_libraries(
        engine_model_animation_script_test PRIVATE engine::script engine::animation engine::world
                                                   engine::resource engine::app engine::log)
    engine_apply_options(engine_model_animation_script_test)
    add_test(NAME engine.model_animation_script COMMAND engine_model_animation_script_test)
endif()

if(TARGET engine_font)
    add_executable(engine_font_system_test runtime/font/font_system_test.cpp)
    target_link_libraries(engine_font_system_test PRIVATE engine::font engine::world
                                                          engine::resource)
    engine_apply_options(engine_font_system_test)
    add_test(NAME engine.font_system COMMAND engine_font_system_test)
endif()

if(TARGET engine_effects_editor)
    add_executable(engine_effects_pipeline_test plugins/editor/effects/effects_pipeline_test.cpp)
    target_link_libraries(engine_effects_pipeline_test
                          PRIVATE engine::effects_editor engine::effects_backend engine::resource)
    engine_apply_options(engine_effects_pipeline_test)
    add_test(NAME editor.effects_pipeline COMMAND engine_effects_pipeline_test
                                                  "${PROJECT_SOURCE_DIR}")
    set_tests_properties(editor.effects_pipeline PROPERTIES TIMEOUT 120)
endif()

if(TARGET engine_effects)
    add_executable(engine_effects_runtime_test plugins/runtime/effects/effects_runtime_test.cpp)
    target_link_libraries(engine_effects_runtime_test
                          PRIVATE engine::effects engine::effects_backend SDL3::SDL3 bgfx::bgfx)
    engine_apply_options(engine_effects_runtime_test)
    if(ENGINE_ENABLE_GPU_TESTS)
        add_test(NAME engine.effects_runtime COMMAND engine_effects_runtime_test
                                                     "${PROJECT_SOURCE_DIR}")
        set_tests_properties(engine.effects_runtime PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
    endif()
endif()

if(TARGET engine_navigation)
    add_executable(engine_navigation_test runtime/navigation/navigation_test.cpp)
    target_link_libraries(engine_navigation_test PRIVATE engine::navigation)
    engine_apply_options(engine_navigation_test)
    add_test(NAME engine.navigation COMMAND engine_navigation_test)
    set_tests_properties(engine.navigation PROPERTIES TIMEOUT 60)
endif()

if(TARGET engine_navigation AND TARGET engine_script)
    add_executable(engine_navigation_script_test plugins/runtime/script/navigation_script_test.cpp)
    target_link_libraries(
        engine_navigation_script_test PRIVATE engine::script engine::navigation engine::world
                                              engine::resource engine::app engine::log)
    engine_apply_options(engine_navigation_script_test)
    add_test(NAME engine.navigation_script COMMAND engine_navigation_script_test)
    set_tests_properties(engine.navigation_script PROPERTIES TIMEOUT 60)
endif()

if(TARGET engine_navigation AND TARGET engine_terrain)
    add_executable(engine_navigation_terrain_test runtime/navigation/navigation_terrain_test.cpp)
    target_link_libraries(engine_navigation_terrain_test PRIVATE engine::navigation engine::terrain)
    engine_apply_options(engine_navigation_terrain_test)
    add_test(NAME engine.navigation_terrain COMMAND engine_navigation_terrain_test)
    set_tests_properties(engine.navigation_terrain PROPERTIES TIMEOUT 60)
endif()

if(TARGET engine_terrain)
    add_executable(engine_terrain_runtime_test plugins/runtime/terrain/terrain_runtime_test.cpp)
    target_link_libraries(engine_terrain_runtime_test PRIVATE engine::terrain)
    engine_apply_options(engine_terrain_runtime_test)
    add_test(NAME engine.terrain_runtime COMMAND engine_terrain_runtime_test)
    set_tests_properties(engine.terrain_runtime PROPERTIES TIMEOUT 60)

    add_executable(engine_terrain_collision_benchmark
                   plugins/runtime/terrain/terrain_collision_benchmark.cpp)
    target_link_libraries(engine_terrain_collision_benchmark PRIVATE engine::terrain)
    engine_apply_options(engine_terrain_collision_benchmark)
endif()

if(TARGET engine_terrain_editor)
    add_executable(engine_terrain_pipeline_test plugins/editor/terrain/terrain_pipeline_test.cpp)
    target_link_libraries(engine_terrain_pipeline_test PRIVATE engine::terrain_editor)
    engine_apply_options(engine_terrain_pipeline_test)
    add_test(NAME editor.terrain_pipeline COMMAND engine_terrain_pipeline_test)
    set_tests_properties(editor.terrain_pipeline PROPERTIES TIMEOUT 60)
endif()

if(TARGET engine_destruction)
    foreach(_test graph runtime)
        add_executable(engine_destruction_${_test}_test
                       plugins/runtime/destruction/destruction_${_test}_test.cpp)
        target_link_libraries(engine_destruction_${_test}_test PRIVATE engine::destruction)
        engine_apply_options(engine_destruction_${_test}_test)
        add_test(NAME engine.destruction_${_test} COMMAND engine_destruction_${_test}_test)
        set_tests_properties(engine.destruction_${_test} PROPERTIES TIMEOUT 60)
    endforeach()
endif()
if(TARGET engine_destruction_editor)
    foreach(_test authoring pipeline)
        add_executable(engine_destruction_${_test}_test
                       plugins/editor/destruction/destruction_${_test}_test.cpp)
        target_link_libraries(engine_destruction_${_test}_test PRIVATE engine::destruction_editor)
        engine_apply_options(engine_destruction_${_test}_test)
        add_test(NAME editor.destruction_${_test} COMMAND engine_destruction_${_test}_test)
        set_tests_properties(editor.destruction_${_test} PROPERTIES TIMEOUT 60)
    endforeach()
endif()
if(TARGET engine_destruction_editor AND TARGET engine_editor)
    add_executable(engine_destruction_commands_test
                   plugins/editor/destruction/destruction_commands_test.cpp)
    target_link_libraries(engine_destruction_commands_test PRIVATE engine::destruction_editor
                                                                   engine::editor)
    target_compile_definitions(engine_destruction_commands_test PRIVATE JSON_NOEXCEPTION=1)
    engine_apply_options(engine_destruction_commands_test)
    add_test(NAME editor.destruction_commands COMMAND engine_destruction_commands_test)
    set_tests_properties(editor.destruction_commands PROPERTIES TIMEOUT 60)
endif()

if(TARGET engine_navigation_crowd)
    add_executable(engine_navigation_crowd_test
                   plugins/runtime/navigation_crowd/navigation_crowd_test.cpp)
    target_link_libraries(engine_navigation_crowd_test PRIVATE engine::navigation_crowd)
    engine_apply_options(engine_navigation_crowd_test)
    add_test(NAME engine_navigation_crowd COMMAND engine_navigation_crowd_test)
    set_tests_properties(engine_navigation_crowd PROPERTIES TIMEOUT 60)
endif()

if(TARGET engine_navigation_crowd AND TARGET engine_script)
    add_executable(engine_navigation_agent_script_test
                   plugins/runtime/script/navigation_agent_script_test.cpp)
    target_link_libraries(
        engine_navigation_agent_script_test
        PRIVATE engine::script
                engine::navigation_crowd
                engine::world
                engine::physics
                engine::window
                engine::render
                engine::resource
                engine::app
                engine::log)
    target_include_directories(engine_navigation_agent_script_test
                               PRIVATE "${PROJECT_SOURCE_DIR}/src")
    target_compile_definitions(engine_navigation_agent_script_test
                               PRIVATE ENGINE_HAS_NAVIGATION_CROWD_PLUGIN=1)
    engine_apply_options(engine_navigation_agent_script_test)
    add_test(NAME engine.navigation_agent_script COMMAND engine_navigation_agent_script_test)
    set_tests_properties(engine.navigation_agent_script PROPERTIES TIMEOUT 60)
endif()

if(TARGET engine_navigation_crowd_editor)
    add_executable(engine_navigation_agent_codec_test
                   plugins/editor/navigation_crowd/navigation_agent_codec_test.cpp)
    target_link_libraries(engine_navigation_agent_codec_test
                          PRIVATE engine::navigation_crowd_editor)
    engine_apply_options(engine_navigation_agent_codec_test)
    add_test(NAME editor.navigation_agent_codec COMMAND engine_navigation_agent_codec_test)
    set_tests_properties(editor.navigation_agent_codec PROPERTIES TIMEOUT 60)
endif()

if(TARGET engine_runtime)
    add_executable(engine_gameplay_session_test runtime/gameplay/gameplay_session_test.cpp)
    target_link_libraries(engine_gameplay_session_test PRIVATE engine::runtime)
    engine_apply_options(engine_gameplay_session_test)
    add_test(NAME engine.gameplay_session COMMAND engine_gameplay_session_test)
    set_tests_properties(engine.gameplay_session PROPERTIES TIMEOUT 60)

    add_executable(engine_runtime_settings_test runtime/app/runtime_settings_test.cpp)
    target_link_libraries(engine_runtime_settings_test PRIVATE engine::runtime)
    engine_apply_options(engine_runtime_settings_test)
    add_test(NAME engine.runtime_settings COMMAND engine_runtime_settings_test)
    set_tests_properties(engine.runtime_settings PROPERTIES TIMEOUT 30)
endif()

if(TARGET engine_editor)
    add_executable(engine_project_runtime_settings_test
                   editor/project/project_runtime_settings_test.cpp)
    target_link_libraries(engine_project_runtime_settings_test PRIVATE engine::editor engine::runtime)
    engine_apply_options(engine_project_runtime_settings_test)
    add_test(NAME editor.runtime_settings COMMAND engine_project_runtime_settings_test
        "${PROJECT_SOURCE_DIR}/projects/showcase")
    set_tests_properties(editor.runtime_settings PROPERTIES TIMEOUT 30)
endif()

if(TARGET engine_script AND TARGET engine_terrain AND TARGET engine_destruction)
    add_executable(engine_terrain_destruction_script_test
                   plugins/runtime/script/terrain_destruction_script_test.cpp)
    target_link_libraries(engine_terrain_destruction_script_test PRIVATE
        engine::script engine::terrain engine::destruction engine::task
        engine::physics engine::world engine::resource engine::app engine::log)
    engine_apply_options(engine_terrain_destruction_script_test)
    engine_copy_script_data(engine_terrain_destruction_script_test)
    add_test(NAME engine.terrain_destruction_script COMMAND engine_terrain_destruction_script_test)
    set_tests_properties(engine.terrain_destruction_script PROPERTIES TIMEOUT 60)
endif()

if(TARGET lightmapper::lightmapper)
    add_executable(engine_lighting_bake_math_test
                   plugins/editor/scene_bake/lighting_bake_math_test.cpp)
    target_link_libraries(engine_lighting_bake_math_test PRIVATE lightmapper::lightmapper)
    target_compile_features(engine_lighting_bake_math_test PRIVATE cxx_std_20)
    engine_apply_options(engine_lighting_bake_math_test)
    add_test(NAME editor.lighting_bake_math COMMAND engine_lighting_bake_math_test)
endif()

if(TARGET engine_asset_pipeline)
    add_executable(engine_light_probe_resource_test
                   editor/asset_pipeline/light_probe_resource_test.cpp)
    target_link_libraries(engine_light_probe_resource_test PRIVATE engine::asset_pipeline engine::resource)
    target_include_directories(engine_light_probe_resource_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    engine_apply_options(engine_light_probe_resource_test)
    add_test(NAME resource.light_probe COMMAND engine_light_probe_resource_test)
endif()

if(TARGET engine_scene_bake)
    add_executable(engine_scene_lighting_files_test plugins/editor/scene_bake/scene_lighting_files_test.cpp
        "${PROJECT_SOURCE_DIR}/src/engine/plugins/editor/scene_bake/scene_bake_files.cpp")
    target_link_libraries(engine_scene_lighting_files_test PRIVATE engine::asset_pipeline engine::resource)
    target_include_directories(engine_scene_lighting_files_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    get_target_property(_bake_toml_includes tomlplusplus::tomlplusplus INTERFACE_INCLUDE_DIRECTORIES)
    target_include_directories(engine_scene_lighting_files_test SYSTEM PRIVATE ${_bake_toml_includes})
    target_compile_definitions(engine_scene_lighting_files_test PRIVATE TOML_HEADER_ONLY=1 TOML_EXCEPTIONS=0)
    engine_apply_options(engine_scene_lighting_files_test)
    add_test(NAME editor.scene_lighting_files COMMAND engine_scene_lighting_files_test)
endif()

# Shared fixtures are private to test targets.
get_property(_engine_test_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(_target IN LISTS _engine_test_targets)
    target_include_directories(${_target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/support")
endforeach()

if(TARGET engine_editor AND TARGET engine_asset_pipeline)
    add_executable(engine_prefab_commands_test editor/commands/prefab_commands_test.cpp)
    target_include_directories(engine_prefab_commands_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/support")
    target_link_libraries(engine_prefab_commands_test PRIVATE engine::editor engine::asset_pipeline
                                                             engine::world engine::resource)
    engine_apply_options(engine_prefab_commands_test)
    add_test(NAME editor.prefab_commands COMMAND engine_prefab_commands_test)
endif()
