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

if(ENGINE_ENABLE_V8)
    add_test(NAME engine.v8_cache
             COMMAND "${CMAKE_COMMAND}" "-DENGINE_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/v8-cache-test"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/build/v8_cache_test.cmake")
    set_tests_properties(engine.v8_cache PROPERTIES TIMEOUT 30)
endif()

if(TARGET engine_task)
    add_executable(engine_task_test runtime/task/task_test.cpp)
    target_link_libraries(engine_task_test PRIVATE engine::task)
    engine_apply_options(engine_task_test)
    add_test(NAME runtime.task COMMAND engine_task_test)
    set_tests_properties(runtime.task PROPERTIES TIMEOUT 30)
endif()

if(TARGET engine_platform)
    add_executable(engine_app_log_test runtime/app/app_log_test.cpp)
    target_link_libraries(engine_app_log_test PRIVATE engine::app engine::log Threads::Threads
                                                    SDL3::SDL3)
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

if(TARGET engine_audio AND TARGET engine_resource_graph)
    add_executable(engine_audio_test runtime/audio/audio_test.cpp)
    target_link_libraries(engine_audio_test PRIVATE engine::resource_graph)
    target_include_directories(engine_audio_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    target_link_libraries(engine_audio_test PRIVATE engine::audio engine::world engine::resource
                                                    SDL3::SDL3)
    engine_apply_options(engine_audio_test)
    add_test(NAME engine.audio COMMAND engine_audio_test)
    set_tests_properties(engine.audio PROPERTIES TIMEOUT 60)
endif()

if(TARGET engine_ui_editor AND TARGET engine_asset_import AND TARGET engine_resource_graph)
    add_executable(engine_ui_editor_test plugins/editor/ui/ui_editor_test.cpp)
    target_link_libraries(engine_ui_editor_test PRIVATE engine::resource_graph)
    target_include_directories(engine_ui_editor_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    target_link_libraries(engine_ui_editor_test PRIVATE engine::ui_editor engine::asset_import
        engine::render engine::font engine::input engine::world engine::resource SDL3::SDL3)
    engine_apply_options(engine_ui_editor_test)
    if(ENGINE_ENABLE_GPU_TESTS)
        add_test(NAME editor.ui_editor COMMAND engine_ui_editor_test
            "${PROJECT_SOURCE_DIR}/projects/showcase" "${CMAKE_CURRENT_BINARY_DIR}")
        set_tests_properties(editor.ui_editor PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
    endif()
endif()

if(TARGET engine_script)
    if(ENGINE_ENABLE_TEST_SUPPORT)
    add_executable(engine_project_script_test plugins/runtime/script/project_script_test.cpp)
    target_link_libraries(engine_project_script_test PRIVATE engine::script)
    target_include_directories(engine_project_script_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    engine_apply_options(engine_project_script_test)
    engine_copy_script_data(engine_project_script_test)
    add_test(NAME engine.project_script COMMAND engine_project_script_test)
    set_tests_properties(engine.project_script PROPERTIES TIMEOUT 60)

    endif()

    if(ENGINE_ENABLE_TEST_SUPPORT AND TARGET engine_resource_graph)
        add_executable(engine_script_runtime_test plugins/runtime/script/script_runtime_test.cpp)
        target_link_libraries(engine_script_runtime_test PRIVATE engine::resource_graph)
        target_include_directories(engine_script_runtime_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
        target_link_libraries(engine_script_runtime_test PRIVATE unofficial::libvpx::libvpx)
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

        target_compile_definitions(engine_script_runtime_test PRIVATE ENGINE_TEST_RESOURCE_GRAPH=1)
        add_test(NAME engine.script_runtime
                 COMMAND engine_script_runtime_test
                         "${CMAKE_CURRENT_BINARY_DIR}/script-runtime-test-output")
        set_tests_properties(engine.script_runtime PROPERTIES TIMEOUT 60)
    endif()
endif()

if(TARGET engine_asset_sync)
    add_executable(engine_asset_sync_test plugins/editor/asset_sync/asset_sync_test.cpp)
    target_include_directories(engine_asset_sync_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    target_link_libraries(engine_asset_sync_test PRIVATE engine::asset_sync engine::resource
                                                         engine_editor_lock SDL3::SDL3
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
    if(NOT TARGET engine_camera_mcp_tools)
        list(APPEND _script_tooling_options --without-camera)
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
    if(TARGET engine_resource_graph)
        add_executable(engine_modeling_commands_test plugins/editor/modeling/modeling_commands_test.cpp)
        target_link_libraries(engine_modeling_commands_test PRIVATE engine::resource_graph)
        target_include_directories(engine_modeling_commands_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
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
    if(TARGET engine_resource_graph)
        add_executable(engine_physics_audio_commands_test
                       editor/commands/physics_audio_commands_test.cpp)
        target_link_libraries(engine_physics_audio_commands_test PRIVATE engine::resource_graph)
        target_include_directories(engine_physics_audio_commands_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
        target_link_libraries(engine_physics_audio_commands_test PRIVATE engine::editor engine::world
                                                                         engine::resource)
        engine_apply_options(engine_physics_audio_commands_test)
        add_test(NAME editor.physics_audio_commands COMMAND engine_physics_audio_commands_test)
        set_tests_properties(editor.physics_audio_commands PROPERTIES TIMEOUT 30)
        add_executable(engine_editor_commands_test editor/commands/editor_commands_test.cpp)
        target_link_libraries(engine_editor_commands_test PRIVATE engine::resource_graph)
        target_include_directories(engine_editor_commands_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
        target_include_directories(engine_editor_commands_test
                                   PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
        target_link_libraries(engine_editor_commands_test PRIVATE engine::editor engine::world
                                                                  engine::resource)
        engine_apply_options(engine_editor_commands_test)
        add_test(NAME editor.commands COMMAND engine_editor_commands_test)
        set_tests_properties(editor.commands PROPERTIES TIMEOUT 30)

        add_executable(engine_editor_environment_test editor/host/editor_environment_test.cpp)
        target_link_libraries(engine_editor_environment_test PRIVATE engine::resource_graph)
        target_include_directories(engine_editor_environment_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
        target_link_libraries(engine_editor_environment_test PRIVATE engine::editor engine::project
                                                                     engine::app engine::resource)
        engine_apply_options(engine_editor_environment_test)
        add_test(NAME engine.editor_environment COMMAND engine_editor_environment_test
                                                        "${CMAKE_CURRENT_BINARY_DIR}")
    endif()

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

if(ENGINE_ENABLE_RUNTIME_DEPS AND TARGET engine_resource_graph)
    add_executable(engine_mesh_lod_selection_test runtime/render/mesh_lod_selection_test.cpp)
    target_include_directories(engine_mesh_lod_selection_test
                               PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    target_link_libraries(engine_mesh_lod_selection_test PRIVATE engine::render)
    engine_apply_options(engine_mesh_lod_selection_test)
    add_test(NAME runtime.mesh_lod_selection COMMAND engine_mesh_lod_selection_test)

    add_executable(engine_frustum_culling_test runtime/render/frustum_culling_test.cpp)
    target_include_directories(engine_frustum_culling_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    target_link_libraries(engine_frustum_culling_test PRIVATE engine::render bgfx::bx)
    engine_apply_options(engine_frustum_culling_test)
    add_test(NAME runtime.frustum_culling COMMAND engine_frustum_culling_test)

    add_executable(engine_planar_reflection_test runtime/render/planar_reflection_test.cpp)
    target_include_directories(engine_planar_reflection_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    target_link_libraries(engine_planar_reflection_test PRIVATE engine::render bgfx::bx)
    engine_apply_options(engine_planar_reflection_test)
    add_test(NAME engine.planar_reflection COMMAND engine_planar_reflection_test)
    if(TARGET engine_resource_graph)
        add_executable(engine_render_foundation_test runtime/render/render_foundation_test.cpp)
        target_link_libraries(engine_render_foundation_test PRIVATE engine::resource_graph)
        target_include_directories(engine_render_foundation_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
        target_link_libraries(engine_render_foundation_test PRIVATE engine::world engine::resource
                                                                    engine::render)

        target_compile_definitions(engine_render_foundation_test
                                   PRIVATE ENGINE_TEST_RESOURCE_GRAPH=1)
        engine_apply_options(engine_render_foundation_test)
        add_test(NAME engine.render_foundation COMMAND engine_render_foundation_test)
        set_tests_properties(engine.render_foundation PROPERTIES TIMEOUT 30)
        add_executable(engine_window_render_test runtime/render/window_render_test.cpp)
        target_link_libraries(engine_window_render_test PRIVATE engine::resource_graph)
        target_include_directories(engine_window_render_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    endif()

    if(TARGET engine_resource_graph)
        add_executable(engine_scene_hdr_capture_test runtime/render/scene_hdr_capture_test.cpp)
        target_link_libraries(engine_scene_hdr_capture_test PRIVATE engine::resource_graph)
        target_include_directories(engine_scene_hdr_capture_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
        target_link_libraries(engine_scene_hdr_capture_test PRIVATE engine::runtime engine::window
                              engine::render engine::world engine::resource SDL3::SDL3 bgfx::bgfx
                              bgfx::bimg)
        engine_apply_options(engine_scene_hdr_capture_test)
    endif()

    if(TARGET engine_resource_graph)
        add_executable(engine_scene_hdr_transparency_test runtime/render/scene_hdr_transparency_test.cpp)
        target_link_libraries(engine_scene_hdr_transparency_test PRIVATE engine::resource_graph)
        target_include_directories(engine_scene_hdr_transparency_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
        target_link_libraries(engine_scene_hdr_transparency_test PRIVATE engine::runtime engine::window
                              engine::render engine::world engine::resource SDL3::SDL3)
        engine_apply_options(engine_scene_hdr_transparency_test)
    endif()

    add_executable(engine_hdr_extension_test runtime/render/hdr_extension_test.cpp)
    set(_extension_shader_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/hdr-extension-test")
    engine_render_shader_profile(_extension_shader_platform _extension_shader_profile)
    foreach(_stage IN ITEMS vert frag)
        if(_stage STREQUAL "vert")
            set(_shader_type vertex)
        else()
            set(_shader_type fragment)
        endif()
        set(_extension_header "${_extension_shader_dir}/hdr_extension_test_${_stage}.bin.h")
        add_custom_command(
            OUTPUT "${_extension_header}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${_extension_shader_dir}"
            COMMAND "${ENGINE_SHADERC_EXECUTABLE}"
                    -f "${CMAKE_CURRENT_SOURCE_DIR}/runtime/render/hdr_extension_test.${_stage}.sc"
                    -o "${_extension_header}" --type "${_shader_type}"
                    --platform "${_extension_shader_platform}" --profile "${_extension_shader_profile}"
                    --varyingdef "${PROJECT_SOURCE_DIR}/src/engine/runtime/render/shaders/varying.def.sc"
                    -i "${ENGINE_BGFX_SHADER_INCLUDE_DIR}"
                    --bin2c "hdr_extension_test_${_stage}"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/runtime/render/hdr_extension_test.${_stage}.sc"
                    "${PROJECT_SOURCE_DIR}/src/engine/runtime/render/shaders/varying.def.sc"
                    "${ENGINE_BGFX_SHADER_INCLUDE_DIR}/bgfx_shader.sh" "${ENGINE_SHADERC_EXECUTABLE}"
            VERBATIM)
        target_sources(engine_hdr_extension_test PRIVATE "${_extension_header}")
    endforeach()
    target_include_directories(engine_hdr_extension_test PRIVATE "${_extension_shader_dir}")
    target_link_libraries(engine_hdr_extension_test PRIVATE engine::runtime engine::window
                          engine::render engine::world SDL3::SDL3 bgfx::bgfx)
    engine_apply_options(engine_hdr_extension_test)
    add_executable(engine_pbr_hdr_test runtime/render/pbr_hdr_test.cpp)
    # An isolated RGBA16F draw includes the production direct-light shader.
    # Keep this test-only shader out of the runtime and its public interfaces.
    engine_render_shader_profile(_test_shader_platform _test_shader_profile)
    set(_pbr_test_shader_dir "${CMAKE_CURRENT_SOURCE_DIR}/runtime/render")
    set(_pbr_shader_dir "${PROJECT_SOURCE_DIR}/src/engine/runtime/render/shaders")
    set(_pbr_test_generated "${CMAKE_CURRENT_BINARY_DIR}/generated/pbr-test")
    foreach(_stage IN ITEMS vert frag)
        if(_stage STREQUAL "vert")
            set(_shader_type vertex)
        else()
            set(_shader_type fragment)
        endif()
        set(_shader_header "${_pbr_test_generated}/pbr_hdr_test_${_stage}.bin.h")
        add_custom_command(
            OUTPUT "${_shader_header}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${_pbr_test_generated}"
            COMMAND "${ENGINE_SHADERC_EXECUTABLE}"
                    -f "${_pbr_test_shader_dir}/pbr_hdr_test.${_stage}.sc"
                    -o "${_shader_header}" --type "${_shader_type}"
                    --platform "${_test_shader_platform}" --profile "${_test_shader_profile}"
                    --varyingdef "${_pbr_shader_dir}/varying.def.sc"
                    -i "${ENGINE_BGFX_SHADER_INCLUDE_DIR}" -i "${_pbr_shader_dir}"
                    --bin2c "pbr_hdr_test_${_stage}"
            DEPENDS "${_pbr_test_shader_dir}/pbr_hdr_test.${_stage}.sc"
                    "${_pbr_shader_dir}/varying.def.sc" "${_pbr_shader_dir}/pbr_helpers.sh"
                    "${_pbr_shader_dir}/pbr_lighting.sh"
                    "${ENGINE_BGFX_SHADER_INCLUDE_DIR}/bgfx_shader.sh" "${ENGINE_SHADERC_EXECUTABLE}"
            VERBATIM)
        target_sources(engine_pbr_hdr_test PRIVATE "${_shader_header}")
    endforeach()
    target_include_directories(engine_pbr_hdr_test PRIVATE "${_pbr_test_generated}")
    target_link_libraries(engine_pbr_hdr_test PRIVATE engine::runtime engine::window engine::render
                                                   engine::world SDL3::SDL3 bgfx::bgfx
                                                   basisu::basisu_encoder)
    engine_apply_options(engine_pbr_hdr_test)
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
                bgfx::bimg_decode
                basisu::basisu_encoder)
    engine_apply_options(engine_window_render_test)
    target_compile_definitions(engine_window_render_test PRIVATE
                               ENGINE_TEST_RUNTIME_SETTINGS="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/runtime-settings.json"
                               ENGINE_RENDER_TEST_ARTIFACTS_DIR="${CMAKE_BINARY_DIR}/test-artifacts/window-render")
    if(TARGET engine_resource_graph)

        target_compile_definitions(engine_window_render_test PRIVATE ENGINE_TEST_RESOURCE_GRAPH=1)
    endif()
    if(ENGINE_ENABLE_GPU_TESTS)
        add_test(NAME engine.scene_hdr_capture COMMAND engine_scene_hdr_capture_test)
        set_tests_properties(engine.scene_hdr_capture PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
        add_test(NAME engine.scene_hdr_transparency COMMAND engine_scene_hdr_transparency_test)
        set_tests_properties(engine.scene_hdr_transparency PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
        add_test(NAME engine.hdr_extension_capture COMMAND engine_hdr_extension_test)
        set_tests_properties(engine.hdr_extension_capture PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
        add_test(NAME engine.window_render COMMAND engine_window_render_test)
        set_tests_properties(engine.window_render PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
        add_test(NAME engine.pbr_hdr_direct_light COMMAND engine_pbr_hdr_test)
        set_tests_properties(engine.pbr_hdr_direct_light
                             PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE SKIP_RETURN_CODE 77)
    endif()
endif()

if(ENGINE_ENABLE_ASSET_PIPELINE_DEPS)
    add_executable(engine_mesh_lod_test editor/mesh/mesh_lod_test.cpp)
    target_include_directories(engine_mesh_lod_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    get_target_property(_mesh_lod_test_toml_includes tomlplusplus::tomlplusplus
                        INTERFACE_INCLUDE_DIRECTORIES)
    target_include_directories(engine_mesh_lod_test SYSTEM PRIVATE ${_mesh_lod_test_toml_includes})
    target_compile_definitions(engine_mesh_lod_test PRIVATE TOML_HEADER_ONLY=1 TOML_EXCEPTIONS=0)
    target_link_libraries(engine_mesh_lod_test PRIVATE engine_editor_mesh engine::resource)
    engine_apply_options(engine_mesh_lod_test)
    add_test(NAME editor.mesh_lod COMMAND engine_mesh_lod_test)
    if(TARGET engine_editor)
        add_executable(engine_mesh_lod_pipeline_test editor/asset_pipeline/mesh_lod_pipeline_test.cpp)
        target_link_libraries(engine_mesh_lod_pipeline_test
                              PRIVATE engine::editor engine::asset_pipeline engine::resource)
        engine_apply_options(engine_mesh_lod_pipeline_test)
        add_test(NAME editor.mesh_lod_pipeline COMMAND engine_mesh_lod_pipeline_test)
        set_tests_properties(editor.mesh_lod_pipeline PROPERTIES TIMEOUT 120)
    endif()

    add_executable(engine_mesh_pipeline_test editor/asset_pipeline/mesh_pipeline_test.cpp)
    target_link_libraries(engine_mesh_pipeline_test PRIVATE engine::asset_pipeline engine::resource)
    engine_apply_options(engine_mesh_pipeline_test)
    add_test(NAME editor.mesh_pipeline COMMAND engine_mesh_pipeline_test)
    set_tests_properties(editor.mesh_pipeline PROPERTIES TIMEOUT 120)
    add_executable(engine_audio_pipeline_test editor/asset_pipeline/audio_pipeline_test.cpp)
    target_link_libraries(engine_audio_pipeline_test PRIVATE engine::asset_pipeline
                                                             engine::resource)
    engine_apply_options(engine_audio_pipeline_test)
    add_test(NAME editor.audio_pipeline COMMAND engine_audio_pipeline_test)
    set_tests_properties(editor.audio_pipeline PROPERTIES TIMEOUT 120)
    if(TARGET engine_resource_graph)
        add_executable(engine_texture_pipeline_test editor/asset_pipeline/texture_pipeline_test.cpp)
        target_link_libraries(engine_texture_pipeline_test PRIVATE engine::resource_graph)
        target_include_directories(engine_texture_pipeline_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
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
    endif()

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
                "${PROJECT_SOURCE_DIR}/projects/showcase" --shaderc "${ENGINE_SHADERC_EXECUTABLE}"
                --shader-includes "${ENGINE_BGFX_SHADER_INCLUDE_DIR}")
        set_tests_properties(editor.asset_pipeline_scripts PROPERTIES TIMEOUT 180)
        add_test(NAME editor.portable_tools
                 COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/editor/asset_pipeline/editor_tools_test.py"
                         --root "${PROJECT_SOURCE_DIR}" --node "${ENGINE_NODE_EXECUTABLE}")
        set_tests_properties(editor.portable_tools PROPERTIES TIMEOUT 90)
    endif()

    engine_render_shader_profile(_asset_shader_platform _asset_shader_profile)
    add_test(
        NAME engine.asset_tools
        COMMAND
            "${CMAKE_COMMAND}" "-DENGINE_SHADERC=${ENGINE_BGFX_SHADERC_EXECUTABLE}"
            "-DENGINE_TEXTUREC=${ENGINE_BGFX_TEXTUREC_EXECUTABLE}"
            "-DENGINE_GEOMETRYC=${ENGINE_BGFX_GEOMETRYC_EXECUTABLE}"
            "-DENGINE_SHADER_PLATFORM=${_asset_shader_platform}"
            "-DENGINE_SHADER_PROFILE=${_asset_shader_profile}"
            "-DENGINE_SHADER_INCLUDE_DIR=${ENGINE_BGFX_SHADER_INCLUDE_DIR}"
            "-DENGINE_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/asset-tools-output" -P
            "${CMAKE_CURRENT_SOURCE_DIR}/editor/asset_pipeline/asset_tools_test.cmake")
    set_tests_properties(engine.asset_tools PROPERTIES TIMEOUT 100)
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
    if(TARGET engine_resource_graph)
        add_executable(engine_resource_catalog_test runtime/resource/resource_catalog_test.cpp)
        target_link_libraries(engine_resource_catalog_test PRIVATE engine::resource_graph)
        target_include_directories(engine_resource_catalog_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
        target_link_libraries(engine_resource_catalog_test PRIVATE engine::world engine::resource)
        engine_apply_options(engine_resource_catalog_test)
        add_test(NAME engine.resource_catalog
                 COMMAND engine_resource_catalog_test
                         "${CMAKE_CURRENT_BINARY_DIR}/resource-catalog-test-output")
        set_tests_properties(engine.resource_catalog PROPERTIES TIMEOUT 30)

        add_executable(engine_scene_test runtime/scene/scene_test.cpp)
        target_link_libraries(engine_scene_test PRIVATE engine::resource_graph)
        target_include_directories(engine_scene_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")

        target_link_libraries(engine_scene_test PRIVATE engine::world)
        engine_apply_options(engine_scene_test)
        add_test(NAME engine.scene COMMAND engine_scene_test)
        set_tests_properties(engine.scene PROPERTIES TIMEOUT 30)
    endif()

    if(TARGET engine_resource_graph)
        add_executable(engine_ecs_scene_test runtime/scene/ecs_scene_test.cpp)
        target_link_libraries(engine_ecs_scene_test PRIVATE engine::resource_graph)
        target_include_directories(engine_ecs_scene_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
        target_link_libraries(engine_ecs_scene_test PRIVATE engine::world engine::scene_ecs)
        engine_apply_options(engine_ecs_scene_test)
        add_test(NAME engine.ecs_scene COMMAND engine_ecs_scene_test)
        set_tests_properties(engine.ecs_scene PROPERTIES TIMEOUT 30)
    endif()

    if(TARGET engine_resource_graph)
        add_executable(engine_prefab_test runtime/scene/prefab_test.cpp)
        target_link_libraries(engine_prefab_test PRIVATE engine::resource_graph)
        target_include_directories(engine_prefab_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
        target_link_libraries(engine_prefab_test PRIVATE engine::world engine::resource)
        if(TARGET engine_character_controller)
            target_link_libraries(engine_prefab_test PRIVATE engine::character_controller)
            target_compile_definitions(engine_prefab_test PRIVATE ENGINE_TEST_CHARACTER_CONTROLLER=1)
        endif()
        engine_apply_options(engine_prefab_test)
        add_test(NAME engine.prefab COMMAND engine_prefab_test)

        add_executable(engine_scene_data_test runtime/scene/scene_data_test.cpp)
        target_link_libraries(engine_scene_data_test PRIVATE engine::resource_graph)
        target_include_directories(engine_scene_data_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
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
    if(TARGET engine_resource_graph)
        add_executable(engine_scene_persistence_test editor/scene/scene_persistence_test.cpp)
        target_link_libraries(engine_scene_persistence_test PRIVATE engine::resource_graph)
        target_include_directories(engine_scene_persistence_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
        target_include_directories(engine_scene_persistence_test
                                   PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
        target_link_libraries(
            engine_scene_persistence_test
            PRIVATE engine::modeling_editor engine::modeling_importer engine::editor
                    engine::asset_pipeline engine::world engine::resource)
        engine_apply_options(engine_scene_persistence_test)
        add_test(NAME editor.scene_persistence COMMAND engine_scene_persistence_test)
    endif()
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

if(ENGINE_SHADERC_EXECUTABLE)
    add_test(NAME engine.shader_toolchain
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/build/shader_toolchain_test.py"
            --cmake "${CMAKE_COMMAND}" --source "${PROJECT_SOURCE_DIR}"
            --shaderc "${ENGINE_SHADERC_EXECUTABLE}")
    set_tests_properties(engine.shader_toolchain PROPERTIES TIMEOUT 120)
endif()

add_test(
    NAME engine.architecture_boundaries
    COMMAND
        "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/check_architecture.py" --root
        "${PROJECT_SOURCE_DIR}" --targets "${PROJECT_BINARY_DIR}/architecture" --config "$<CONFIG>")
add_test(NAME engine.architecture_rules
         COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/build/architecture_rules_test.py")

if(TARGET engine_world)
    if(TARGET engine_resource_graph)
        add_executable(engine_component_registry_test runtime/scene/component_registry_test.cpp)
        target_link_libraries(engine_component_registry_test PRIVATE engine::resource_graph)
        target_include_directories(engine_component_registry_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
        target_link_libraries(engine_component_registry_test PRIVATE engine::world engine::resource engine::scene_ecs)
        engine_apply_options(engine_component_registry_test)
        add_test(NAME engine.component_registry COMMAND engine_component_registry_test)
    endif()
endif()

if(TARGET engine_resource)
    if(TARGET engine_resource_graph)
        add_executable(engine_movie_player_test runtime/movie/movie_player_test.cpp)
        target_link_libraries(engine_movie_player_test PRIVATE engine::resource_graph)
        target_include_directories(engine_movie_player_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
        target_link_libraries(engine_movie_player_test PRIVATE engine::movie engine::resource engine::task engine::audio unofficial::libvpx::libvpx)
        engine_apply_options(engine_movie_player_test)
        add_test(NAME resource.movie_player COMMAND engine_movie_player_test)
        set_tests_properties(resource.movie_player PROPERTIES TIMEOUT 30)
    endif()

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
    add_executable(engine_animation_event_interval_test
                   runtime/animation/animation_event_interval_test.cpp)
    target_link_libraries(engine_animation_event_interval_test PRIVATE engine::animation)
    target_include_directories(engine_animation_event_interval_test
        PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    engine_apply_options(engine_animation_event_interval_test)
    add_test(NAME engine.animation_event_interval COMMAND engine_animation_event_interval_test)

    if(TARGET engine_resource_graph)
        add_executable(engine_animation_test runtime/animation/animation_test.cpp)
        target_link_libraries(engine_animation_test PRIVATE engine::resource_graph)
        target_include_directories(engine_animation_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
        target_link_libraries(engine_animation_test PRIVATE engine::animation engine::world
                                                            engine::resource)
        engine_apply_options(engine_animation_test)
        add_test(NAME engine.animation COMMAND engine_animation_test)
    endif()
endif()
if(TARGET engine_asset_import)
    if(TARGET engine_resource_graph)
        add_executable(engine_fbx_pipeline_test plugins/editor/asset_import/fbx_pipeline_test.cpp)
        target_link_libraries(engine_fbx_pipeline_test PRIVATE engine::resource_graph)
        target_include_directories(engine_fbx_pipeline_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
        target_link_libraries(engine_fbx_pipeline_test PRIVATE engine::asset_import engine::resource)
        if(TARGET engine_asset_build)
            target_link_libraries(engine_fbx_pipeline_test PRIVATE engine::asset_build)
            target_compile_definitions(engine_fbx_pipeline_test PRIVATE ENGINE_FBX_TEST_BUILD=1)
        endif()

        target_compile_definitions(engine_fbx_pipeline_test PRIVATE ENGINE_FBX_TEST_GRAPH=1)
        target_compile_definitions(
            engine_fbx_pipeline_test
            PRIVATE ENGINE_FBX_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/projects/showcase/assets/models")
        engine_apply_options(engine_fbx_pipeline_test)
        add_test(NAME editor.fbx_pipeline COMMAND engine_fbx_pipeline_test)
    endif()

    add_executable(engine_fbx_bind_pose_test plugins/editor/asset_import/fbx_bind_pose_test.cpp)
    target_link_libraries(engine_fbx_bind_pose_test PRIVATE engine::asset_import engine_ufbx)
    target_include_directories(engine_fbx_bind_pose_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    target_compile_definitions(
        engine_fbx_bind_pose_test
        PRIVATE ENGINE_FBX_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/projects/showcase/assets/models"
                ENGINE_UFBX_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/third_party/ufbx/data")
    engine_apply_options(engine_fbx_bind_pose_test)
    add_test(NAME editor.fbx_bind_pose COMMAND engine_fbx_bind_pose_test)
endif()

if(TARGET engine_asset_import)
    add_executable(engine_font_pipeline_test plugins/editor/asset_import/font_pipeline_test.cpp)
    target_link_libraries(engine_font_pipeline_test PRIVATE engine::asset_import engine::resource)
    engine_apply_options(engine_font_pipeline_test)
    add_test(NAME editor.font_pipeline
             COMMAND engine_font_pipeline_test
                     "${PROJECT_SOURCE_DIR}/projects/showcase/assets/fonts/lato-regular.ttf")
    set_tests_properties(editor.font_pipeline PROPERTIES TIMEOUT 120)
endif()

if(TARGET engine_asset_import)
    add_executable(engine_import_tool_test editor/asset_pipeline/import_tool_test.cpp)
    target_link_libraries(engine_import_tool_test PRIVATE engine::asset_pipeline)
    engine_apply_options(engine_import_tool_test)
    add_test(NAME editor.import_tool COMMAND engine_import_tool_test)

    add_executable(engine_movie_pipeline_test plugins/editor/asset_import/movie_pipeline_test.cpp)
    target_link_libraries(engine_movie_pipeline_test PRIVATE engine::asset_import engine::resource)
    if(TARGET engine_asset_build)
        target_link_libraries(engine_movie_pipeline_test PRIVATE engine::asset_build)
        target_compile_definitions(engine_movie_pipeline_test PRIVATE ENGINE_TEST_MOVIE_BUILD=1)
    endif()
    engine_apply_options(engine_movie_pipeline_test)
    add_test(NAME editor.movie_pipeline COMMAND engine_movie_pipeline_test
             "${PROJECT_SOURCE_DIR}/projects/showcase/assets/movies/lcals.mp4")

    add_executable(engine_json_importer_test plugins/editor/asset_import/json_importer_test.cpp)
    target_link_libraries(engine_json_importer_test PRIVATE engine::asset_import engine::resource)
    engine_apply_options(engine_json_importer_test)
    add_test(NAME editor.json_importer COMMAND engine_json_importer_test)
endif()

if(TARGET engine_asset_import)
    add_executable(engine_ui_pipeline_test plugins/editor/asset_import/ui_pipeline_test.cpp)
    target_link_libraries(engine_ui_pipeline_test PRIVATE engine::asset_import
                                                          engine::resource bgfx::bimg)
    engine_apply_options(engine_ui_pipeline_test)
    add_test(NAME editor.ui_pipeline
             COMMAND engine_ui_pipeline_test
                     "${PROJECT_SOURCE_DIR}/projects/showcase/assets/fonts/lato-regular.ttf")
    set_tests_properties(editor.ui_pipeline PROPERTIES TIMEOUT 120)
endif()

if(TARGET engine_asset_import
    AND TARGET engine_ui
    AND TARGET engine_font
    AND TARGET engine_render
    AND TARGET engine_window
    AND TARGET engine_world)
    if(TARGET engine_resource_graph)
        add_executable(engine_font_ui_render_test runtime/ui/font_ui_render_test.cpp)
        target_link_libraries(engine_font_ui_render_test PRIVATE engine::resource_graph)
        target_include_directories(engine_font_ui_render_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
        target_link_libraries(
            engine_font_ui_render_test
            PRIVATE engine::asset_import
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
endif()

if(TARGET engine_editor AND TARGET engine_animation)
    if(TARGET engine_resource_graph)
        add_executable(engine_animation_commands_test editor/commands/animation_commands_test.cpp)
        target_link_libraries(engine_animation_commands_test PRIVATE engine::resource_graph)
        target_include_directories(engine_animation_commands_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
        target_link_libraries(engine_animation_commands_test PRIVATE engine::editor engine::animation
                                                                     engine::world engine::resource)
        engine_apply_options(engine_animation_commands_test)
        add_test(NAME editor.animation_commands COMMAND engine_animation_commands_test)
    endif()
endif()

if(TARGET engine_character_controller AND TARGET engine_camera)
    add_executable(engine_camera_test plugins/runtime/camera/camera_test.cpp)
    target_link_libraries(engine_camera_test PRIVATE engine::character_controller engine::camera)
    engine_apply_options(engine_camera_test)
    add_test(NAME engine.camera COMMAND engine_camera_test)
endif()

if(TARGET engine_character_controller)
    if(TARGET engine_resource_graph)
        add_executable(engine_character_controller_test
                       plugins/runtime/character_controller/character_controller_test.cpp)
        target_link_libraries(engine_character_controller_test PRIVATE engine::resource_graph)
        target_include_directories(engine_character_controller_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
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
                                       PRIVATE ENGINE_HAS_CHARACTER_CONTROLLER=1 ENGINE_HAS_CAMERA=1
                                       ENGINE_TEST_RUNTIME_SETTINGS="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/runtime-settings.json")
            engine_apply_options(engine_character_host_test)
            if(ENGINE_ENABLE_GPU_TESTS)
                add_test(NAME engine.character_host COMMAND engine_character_host_test)
                set_tests_properties(engine.character_host PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
            endif()
        endif()
    endif()
endif()

if(TARGET engine_asset_import)
    add_executable(engine_usd_pipeline_test plugins/editor/asset_import/usd_pipeline_test.cpp)
    target_link_libraries(engine_usd_pipeline_test PRIVATE engine::asset_import
                                                           engine::resource)
    target_compile_definitions(
        engine_usd_pipeline_test
        PRIVATE ENGINE_FBX_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/projects/showcase/assets/models")
    engine_apply_options(engine_usd_pipeline_test)
    engine_copy_usd_data(engine_usd_pipeline_test)
    add_test(NAME editor.usd_pipeline COMMAND engine_usd_pipeline_test)
endif()
if(TARGET engine_animation)
    if(TARGET engine_resource_graph)
        add_executable(engine_model_resource_test runtime/resource/model_resource_test.cpp)
        target_link_libraries(engine_model_resource_test PRIVATE engine::resource_graph)
        target_include_directories(engine_model_resource_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
        target_link_libraries(engine_model_resource_test PRIVATE engine::world engine::resource
                                                                 engine::animation)
        engine_apply_options(engine_model_resource_test)
        add_test(NAME engine.model_resource COMMAND engine_model_resource_test)
    endif()
endif()
if(TARGET engine_script
    AND TARGET engine_asset_import
    AND TARGET engine_ui
    AND TARGET engine_font
    AND TARGET engine_render)
    if(TARGET engine_resource_graph)
        add_executable(engine_ui_script_test plugins/runtime/script/ui_script_test.cpp)
        target_link_libraries(engine_ui_script_test PRIVATE engine::resource_graph)
        target_include_directories(engine_ui_script_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
        target_link_libraries(
            engine_ui_script_test
            PRIVATE engine::script
                    engine::app
                    engine::log
                    engine::asset_import
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
endif()

if(TARGET engine_editor AND TARGET engine_animation)
    if(TARGET engine_resource_graph)
        add_executable(engine_model_commands_test editor/commands/model_commands_test.cpp)
        target_link_libraries(engine_model_commands_test PRIVATE engine::resource_graph)
        target_include_directories(engine_model_commands_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
        target_link_libraries(engine_model_commands_test PRIVATE engine::editor engine::world
                                                                 engine::resource engine::animation)
        engine_apply_options(engine_model_commands_test)
        add_test(NAME editor.model_commands COMMAND engine_model_commands_test)
    endif()
endif()

if(TARGET engine_character_controller_editor AND TARGET engine_editor)
    add_executable(engine_character_controller_editor_test
                   plugins/editor/character_controller/character_controller_editor_test.cpp)
    target_link_libraries(engine_character_controller_editor_test
                          PRIVATE engine::character_controller_editor engine::camera_editor engine::editor)
    engine_apply_options(engine_character_controller_editor_test)
    add_test(NAME editor.character_controller COMMAND engine_character_controller_editor_test)
endif()
if(TARGET engine_script AND TARGET engine_animation)
    if(TARGET engine_resource_graph)
        add_executable(engine_model_animation_script_test
                       plugins/runtime/script/model_animation_script_test.cpp)
        target_link_libraries(engine_model_animation_script_test PRIVATE engine::resource_graph)
        target_include_directories(engine_model_animation_script_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
        target_link_libraries(
            engine_model_animation_script_test PRIVATE engine::script engine::animation engine::world
                                                       engine::resource engine::app engine::log)
        engine_apply_options(engine_model_animation_script_test)
        add_test(NAME engine.model_animation_script COMMAND engine_model_animation_script_test)
    endif()
endif()

if(TARGET engine_font)
    if(TARGET engine_resource_graph)
        add_executable(engine_font_system_test runtime/font/font_system_test.cpp)
        target_link_libraries(engine_font_system_test PRIVATE engine::resource_graph)
        target_include_directories(engine_font_system_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")

        target_link_libraries(engine_font_system_test PRIVATE engine::font engine::world
                                                              engine::resource)
        engine_apply_options(engine_font_system_test)
        add_test(NAME engine.font_system COMMAND engine_font_system_test)
    endif()
endif()

if(TARGET engine_effects_editor)
    add_executable(engine_effects_pipeline_test plugins/editor/effects/effects_pipeline_test.cpp)
    target_link_libraries(engine_effects_pipeline_test
                          PRIVATE engine::effects_editor engine::effects_backend engine::resource)
    if(TARGET engine_editor)
        target_link_libraries(engine_effects_pipeline_test PRIVATE engine::editor)
        target_compile_definitions(engine_effects_pipeline_test PRIVATE ENGINE_TEST_EDITOR_COMMANDS=1)
    endif()
    engine_apply_options(engine_effects_pipeline_test)
    add_test(NAME editor.effects_pipeline COMMAND engine_effects_pipeline_test
                                                  "${PROJECT_SOURCE_DIR}")
    set_tests_properties(editor.effects_pipeline PROPERTIES TIMEOUT 120)
endif()

if(TARGET engine_effects)
    if(TARGET engine_resource_graph)
        add_executable(engine_effects_runtime_test plugins/runtime/effects/effects_runtime_test.cpp)
        target_link_libraries(engine_effects_runtime_test PRIVATE engine::resource_graph)
        target_include_directories(engine_effects_runtime_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
        target_link_libraries(engine_effects_runtime_test
                              PRIVATE engine::effects engine::effects_backend SDL3::SDL3 bgfx::bgfx)
        engine_apply_options(engine_effects_runtime_test)
        if(ENGINE_ENABLE_GPU_TESTS)
            add_test(NAME engine.effects_runtime COMMAND engine_effects_runtime_test
                                                         "${PROJECT_SOURCE_DIR}")
            set_tests_properties(engine.effects_runtime PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
        endif()
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
    if(TARGET engine_resource_graph)
        add_executable(engine_navigation_script_test plugins/runtime/script/navigation_script_test.cpp)
        target_link_libraries(engine_navigation_script_test PRIVATE engine::resource_graph)
        target_include_directories(engine_navigation_script_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
        target_link_libraries(
            engine_navigation_script_test PRIVATE engine::script engine::navigation engine::world
                                                  engine::resource engine::app engine::log)
        engine_apply_options(engine_navigation_script_test)
        add_test(NAME engine.navigation_script COMMAND engine_navigation_script_test)
        set_tests_properties(engine.navigation_script PROPERTIES TIMEOUT 60)
    endif()
endif()

if(TARGET engine_navigation AND TARGET engine_terrain)
    add_executable(engine_navigation_terrain_test runtime/navigation/navigation_terrain_test.cpp)
    target_link_libraries(engine_navigation_terrain_test PRIVATE engine::navigation engine::terrain)
    engine_apply_options(engine_navigation_terrain_test)
    add_test(NAME engine.navigation_terrain COMMAND engine_navigation_terrain_test)
    set_tests_properties(engine.navigation_terrain PROPERTIES TIMEOUT 60)
endif()

if(TARGET engine_terrain)
    if(TARGET engine_resource_graph)
        add_executable(engine_terrain_runtime_test plugins/runtime/terrain/terrain_runtime_test.cpp)
        target_link_libraries(engine_terrain_runtime_test PRIVATE engine::resource_graph)
        target_include_directories(engine_terrain_runtime_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
        target_link_libraries(engine_terrain_runtime_test PRIVATE engine::terrain)
        engine_apply_options(engine_terrain_runtime_test)
        add_test(NAME engine.terrain_runtime COMMAND engine_terrain_runtime_test)
        set_tests_properties(engine.terrain_runtime PROPERTIES TIMEOUT 60)
    endif()

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
        if(_test STREQUAL "runtime" AND NOT TARGET engine_resource_graph)
            continue()
        endif()
        add_executable(engine_destruction_${_test}_test
                       plugins/runtime/destruction/destruction_${_test}_test.cpp)
        target_link_libraries(engine_destruction_${_test}_test PRIVATE engine::destruction)
        if(_test STREQUAL "runtime")
            target_link_libraries(engine_destruction_${_test}_test PRIVATE engine::resource_graph)
            target_include_directories(engine_destruction_${_test}_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
        endif()
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
    if(TARGET engine_resource_graph)
        add_executable(engine_destruction_commands_test
                       plugins/editor/destruction/destruction_commands_test.cpp)
        target_link_libraries(engine_destruction_commands_test PRIVATE engine::resource_graph)
        target_include_directories(engine_destruction_commands_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
        target_link_libraries(engine_destruction_commands_test PRIVATE engine::destruction_editor
                                                                       engine::editor)
        target_compile_definitions(engine_destruction_commands_test PRIVATE JSON_NOEXCEPTION=1)
        engine_apply_options(engine_destruction_commands_test)
        add_test(NAME editor.destruction_commands COMMAND engine_destruction_commands_test)
        set_tests_properties(editor.destruction_commands PROPERTIES TIMEOUT 60)
    endif()
endif()

if(TARGET engine_navigation_crowd)
    add_executable(engine_navigation_crowd_test
                   plugins/runtime/navigation_crowd/navigation_crowd_test.cpp)
    target_link_libraries(engine_navigation_crowd_test PRIVATE engine::navigation_crowd)
    engine_apply_options(engine_navigation_crowd_test)
    add_test(NAME engine_navigation_crowd COMMAND engine_navigation_crowd_test)
    set_tests_properties(engine_navigation_crowd PROPERTIES TIMEOUT 60)
endif()

if(TARGET engine_character_controller AND TARGET engine_script AND TARGET engine_resource_graph)
    add_executable(engine_control_script_test plugins/runtime/script/control_script_test.cpp)
    target_link_libraries(engine_control_script_test PRIVATE engine::script
        engine::character_controller engine::resource_graph engine::app engine::log)
    target_include_directories(engine_control_script_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    engine_apply_options(engine_control_script_test)
    add_test(NAME engine.control_script COMMAND engine_control_script_test)
    set_tests_properties(engine.control_script PROPERTIES TIMEOUT 60)
endif()

if(TARGET engine_navigation_crowd AND TARGET engine_script)
    if(TARGET engine_resource_graph)
        add_executable(engine_navigation_agent_script_test
                       plugins/runtime/script/navigation_agent_script_test.cpp)
        target_link_libraries(engine_navigation_agent_script_test PRIVATE engine::resource_graph)
        target_include_directories(engine_navigation_agent_script_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
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
    if(TARGET engine_resource_graph)
        add_executable(engine_runtime_host_test runtime/app/runtime_host_test.cpp)
        target_link_libraries(engine_runtime_host_test PRIVATE engine::resource_graph)
        target_include_directories(engine_runtime_host_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
        target_link_libraries(engine_runtime_host_test PRIVATE engine::runtime engine::app
                                                              engine::resource engine::task
                                                              nlohmann_json::nlohmann_json SDL3::SDL3)
        target_compile_definitions(engine_runtime_host_test PRIVATE
                                   JSON_NOEXCEPTION=1
                                   ENGINE_TEST_RUNTIME_SETTINGS="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/runtime-settings.json")
        target_link_libraries(engine_runtime_host_test PRIVATE unofficial::libvpx::libvpx)
        engine_apply_options(engine_runtime_host_test)
        if(ENGINE_ENABLE_GPU_TESTS)
            add_test(NAME engine.runtime_host COMMAND engine_runtime_host_test)
            set_tests_properties(engine.runtime_host PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
        endif()

        add_executable(engine_gameplay_session_test runtime/gameplay/gameplay_session_test.cpp)
        target_link_libraries(engine_gameplay_session_test PRIVATE engine::resource_graph)
        target_include_directories(engine_gameplay_session_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
        target_link_libraries(engine_gameplay_session_test PRIVATE engine::runtime)
        engine_apply_options(engine_gameplay_session_test)
        add_test(NAME engine.gameplay_session COMMAND engine_gameplay_session_test)
        set_tests_properties(engine.gameplay_session PROPERTIES TIMEOUT 60)
    endif()

    add_executable(engine_runtime_settings_test runtime/app/runtime_settings_test.cpp)
    target_link_libraries(engine_runtime_settings_test PRIVATE engine::runtime
                                                             nlohmann_json::nlohmann_json)
    engine_apply_options(engine_runtime_settings_test)
    add_test(NAME engine.runtime_settings COMMAND engine_runtime_settings_test
        "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/runtime-settings.json")
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
    if(TARGET engine_resource_graph)
        add_executable(engine_terrain_destruction_script_test
                       plugins/runtime/script/terrain_destruction_script_test.cpp)
        target_link_libraries(engine_terrain_destruction_script_test PRIVATE engine::resource_graph)
        target_include_directories(engine_terrain_destruction_script_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
        target_link_libraries(engine_terrain_destruction_script_test PRIVATE
            engine::script engine::terrain engine::destruction engine::task
            engine::physics engine::world engine::resource engine::app engine::log)
        engine_apply_options(engine_terrain_destruction_script_test)
        engine_copy_script_data(engine_terrain_destruction_script_test)
        add_test(NAME engine.terrain_destruction_script COMMAND engine_terrain_destruction_script_test)
        set_tests_properties(engine.terrain_destruction_script PROPERTIES TIMEOUT 60)
    endif()
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
    # This executable deliberately neither links nor constructs the optional dependency loader.
    if(TARGET engine_resource_graph)
        add_executable(engine_scene_bake_core_loader_test
                       plugins/editor/scene_bake/core_loader_gpu_test.cpp)
        target_link_libraries(engine_scene_bake_core_loader_test PRIVATE engine::resource_graph)
        target_include_directories(engine_scene_bake_core_loader_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
        target_link_libraries(engine_scene_bake_core_loader_test PRIVATE engine::scene_bake
            engine::render engine::window engine::world engine::resource engine::task SDL3::SDL3
            bgfx::bgfx bgfx::bx)
        target_include_directories(engine_scene_bake_core_loader_test
            PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
        engine_apply_options(engine_scene_bake_core_loader_test)
        if(ENGINE_ENABLE_GPU_TESTS AND NOT CMAKE_CROSSCOMPILING)
            add_test(NAME editor.scene_bake_core_loader COMMAND engine_scene_bake_core_loader_test)
            set_tests_properties(editor.scene_bake_core_loader PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
        endif()
    endif()

    add_executable(engine_scene_lighting_files_test plugins/editor/scene_bake/scene_lighting_files_test.cpp
        "${PROJECT_SOURCE_DIR}/src/engine/plugins/editor/scene_bake/scene_bake_files.cpp")
    target_link_libraries(engine_scene_lighting_files_test PRIVATE engine::asset_pipeline engine::resource bgfx::bimg_encode)
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
    get_target_property(_target_type ${_target} TYPE)
    if(NOT _target_type STREQUAL "UTILITY" AND NOT _target_type STREQUAL "INTERFACE_LIBRARY")
        target_include_directories(${_target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/support")
    endif()
endforeach()

if(TARGET engine_editor AND TARGET engine_asset_pipeline)
    if(TARGET engine_resource_graph)
        add_executable(engine_prefab_commands_test editor/commands/prefab_commands_test.cpp)
        target_link_libraries(engine_prefab_commands_test PRIVATE engine::resource_graph)
        target_include_directories(engine_prefab_commands_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
        target_include_directories(engine_prefab_commands_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/support")
        target_link_libraries(engine_prefab_commands_test PRIVATE engine::editor engine::asset_pipeline
                                                                 engine::world engine::resource)
        engine_apply_options(engine_prefab_commands_test)
        add_test(NAME editor.prefab_commands COMMAND engine_prefab_commands_test)
    endif()
endif()

if(TARGET engine_runtime AND TARGET engine_render)
    if(TARGET engine_resource_graph)
        add_executable(engine_atmosphere_hex_test runtime/render/atmosphere_hex_test.cpp)
        target_link_libraries(engine_atmosphere_hex_test PRIVATE engine::resource_graph)
        target_include_directories(engine_atmosphere_hex_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
        target_include_directories(engine_atmosphere_hex_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/support")
        target_link_libraries(engine_atmosphere_hex_test PRIVATE engine::runtime engine::window
            engine::render engine::world engine::resource SDL3::SDL3 bgfx::bimg)
        engine_apply_options(engine_atmosphere_hex_test)
        if(NOT CMAKE_CROSSCOMPILING)
            add_test(NAME engine.atmosphere_hex COMMAND engine_atmosphere_hex_test)
            set_tests_properties(engine.atmosphere_hex PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
        endif()
    endif()
endif()

if(TARGET engine_runtime AND TARGET engine_render AND TARGET engine_resource_graph)
    add_executable(engine_render_submission_test runtime/render/render_submission_test.cpp)
    target_link_libraries(engine_render_submission_test PRIVATE engine::resource_graph)
    target_include_directories(engine_render_submission_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    target_include_directories(engine_render_submission_test
                               PRIVATE "${PROJECT_SOURCE_DIR}/src/engine"
                                       "${CMAKE_CURRENT_SOURCE_DIR}/support")
    target_link_libraries(engine_render_submission_test
                          PRIVATE engine::runtime engine::window engine::render engine::world
                                  engine::resource SDL3::SDL3)
    engine_apply_options(engine_render_submission_test)
    engine_enable_profiling(engine_render_submission_test)
    if(ENGINE_ENABLE_GPU_TESTS)
        add_test(NAME engine.render_submission COMMAND engine_render_submission_test)
        set_tests_properties(engine.render_submission
                             PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE SKIP_RETURN_CODE 77)
    endif()
endif()

if(TARGET engine_animation_retarget)
    add_executable(engine_animation_retarget_test
        plugins/runtime/animation_retarget/animation_retarget_test.cpp)
    target_link_libraries(engine_animation_retarget_test
        PRIVATE engine::animation_retarget engine::resource bgfx::bx)
    target_include_directories(engine_animation_retarget_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/support")
    engine_apply_options(engine_animation_retarget_test)
    add_test(NAME engine.animation_retarget COMMAND engine_animation_retarget_test)
endif()

if(TARGET engine_animation_tools)
    add_executable(engine_retarget_importer_test
        plugins/editor/animation_tools/retarget_importer_test.cpp)
    target_link_libraries(engine_retarget_importer_test
        PRIVATE engine::animation_tools engine::asset_pipeline engine::resource engine::task)
    target_include_directories(engine_retarget_importer_test
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/support")
    engine_apply_options(engine_retarget_importer_test)
    add_test(NAME editor.retarget_importer COMMAND engine_retarget_importer_test)
endif()

if(ENGINE_ENABLE_TEST_SUPPORT AND TARGET engine_script AND TARGET engine_animation AND TARGET engine_resource_graph)
    add_executable(engine_animation_state_machine_script_test
        plugins/runtime/script/animation_state_machine_script_test.cpp)
    target_link_libraries(engine_animation_state_machine_script_test PRIVATE engine::resource_graph)
    target_include_directories(engine_animation_state_machine_script_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    target_link_libraries(engine_animation_state_machine_script_test
        PRIVATE engine::script engine::animation engine::world)
    engine_apply_options(engine_animation_state_machine_script_test)
    add_test(NAME engine.animation_state_machine_script COMMAND engine_animation_state_machine_script_test)
endif()

if(TARGET engine_resource)
    add_executable(engine_skeleton_rig_definitions_test
        runtime/resource/skeleton_rig_definitions_test.cpp)
    target_link_libraries(engine_skeleton_rig_definitions_test PRIVATE engine::resource)
    target_include_directories(engine_skeleton_rig_definitions_test
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/support")
    engine_apply_options(engine_skeleton_rig_definitions_test)
    add_test(NAME resource.skeleton_rig_definitions COMMAND engine_skeleton_rig_definitions_test)
endif()

if(ENGINE_ENABLE_TEST_SUPPORT AND TARGET engine_script AND TARGET engine_animation)
    if(TARGET engine_resource_graph)
        add_executable(engine_animation_events_script_test
            plugins/runtime/script/animation_events_script_test.cpp)
        target_link_libraries(engine_animation_events_script_test PRIVATE engine::resource_graph)
        target_include_directories(engine_animation_events_script_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
        target_link_libraries(engine_animation_events_script_test PRIVATE engine::script
            engine::animation engine::world engine::resource engine::app engine::log)
        target_include_directories(engine_animation_events_script_test
            PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/support")
        engine_apply_options(engine_animation_events_script_test)
        add_test(NAME engine.animation_events_script COMMAND engine_animation_events_script_test)
    endif()
endif()

if(TARGET engine_animation)
    if(TARGET engine_resource_graph)
        add_executable(engine_animation_rigging_events_test
            runtime/animation/animation_rigging_events_test.cpp)
        target_link_libraries(engine_animation_rigging_events_test PRIVATE engine::resource_graph)
        target_include_directories(engine_animation_rigging_events_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
        target_link_libraries(engine_animation_rigging_events_test
            PRIVATE engine::animation engine::world engine::resource)
        engine_apply_options(engine_animation_rigging_events_test)
        add_test(NAME engine.animation_rigging_events COMMAND engine_animation_rigging_events_test)
    endif()
endif()

foreach(_animation_test engine_animation_state_machine_script_test engine_animation_rigging_events_test)
    if(TARGET ${_animation_test})
        target_include_directories(${_animation_test} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/support")
    endif()
endforeach()

if(TARGET engine_animation_tools_editor)
    add_executable(engine_animation_authoring_commands_test
        plugins/editor/animation_tools/animation_authoring_commands_test.cpp)
    target_link_libraries(engine_animation_authoring_commands_test
        PRIVATE engine::animation_tools_editor engine::asset_import engine::asset_pipeline
                engine::resource engine::task)
    target_include_directories(engine_animation_authoring_commands_test
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/support")
    target_compile_definitions(engine_animation_authoring_commands_test
        PRIVATE ENGINE_FBX_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/projects/showcase/assets/models")
    engine_apply_options(engine_animation_authoring_commands_test)
    add_test(NAME editor.animation_authoring COMMAND engine_animation_authoring_commands_test)
endif()

if(TARGET engine_animation)
    add_library(engine_test_sampling STATIC ../src/engine/editor/asset_pipeline/animation_sampling.cpp)
    target_include_directories(engine_test_sampling PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    target_link_libraries(engine_test_sampling PUBLIC engine::scene_data PRIVATE engine::resource ozz_animation_offline ozz_animation)
    engine_apply_options(engine_test_sampling)
    foreach(_test engine_animation_test engine_animation_rigging_events_test
        engine_animation_state_machine_script_test engine_animation_events_script_test
        engine_model_animation_script_test
        engine_skeleton_rig_definitions_test
        engine_animation_retarget_test engine_render_foundation_test engine_window_render_test
        engine_retarget_importer_test)
        if(TARGET ${_test})
            target_link_libraries(${_test} PRIVATE engine_test_sampling)
        endif()
    endforeach()
endif()

# 普通构建只带基础检查；模块测试显式指定目标，全量仅通过 engine_tests 构建。
set(_basic_tests engine_framework_test engine_guid_map_test engine_system_test)
get_property(_test_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
add_custom_target(engine_tests)
foreach(_target IN LISTS _test_targets)
    get_target_property(_type "${_target}" TYPE)
    if(_type STREQUAL "EXECUTABLE")
        add_dependencies(engine_tests "${_target}")
    endif()
    if(NOT _target IN_LIST _basic_tests)
        set_property(TARGET "${_target}" PROPERTY EXCLUDE_FROM_ALL TRUE)
    endif()
endforeach()

# Shared external UTF-8 data keeps Unicode coverage out of source literals.
foreach(_target engine_guid_map_test engine_app_log_test engine_key_value_store_test
        engine_resource_read_test engine_ui_editor_test engine_script_runtime_test
        engine_mcp_registry_test engine_texture_pipeline_test engine_scene_data_test
        engine_json_importer_test)
    if(TARGET ${_target})
        target_include_directories(${_target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/support")
    endif()
endforeach()
