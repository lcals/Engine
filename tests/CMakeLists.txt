add_executable(engine_framework_test engine_test.cpp)
target_link_libraries(engine_framework_test PRIVATE engine::core)
engine_apply_options(engine_framework_test)
add_test(NAME engine.framework COMMAND engine_framework_test)

add_executable(engine_guid_map_test guid_map_test.cpp)
target_link_libraries(engine_guid_map_test PRIVATE engine::core)
engine_apply_options(engine_guid_map_test)
add_test(NAME engine.guid_map COMMAND engine_guid_map_test)

add_executable(engine_system_test system_test.cpp)
target_link_libraries(engine_system_test PRIVATE engine::core)
engine_apply_options(engine_system_test)
add_test(NAME engine.system COMMAND engine_system_test)

add_test(NAME engine.v8_cache
         COMMAND "${CMAKE_COMMAND}" "-DENGINE_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/v8-cache-test"
                 -P "${CMAKE_CURRENT_SOURCE_DIR}/v8_cache_test.cmake")
set_tests_properties(engine.v8_cache PROPERTIES TIMEOUT 30)

if(TARGET engine_platform)
    add_executable(engine_app_log_test app_log_test.cpp)
    target_link_libraries(engine_app_log_test PRIVATE engine::app engine::log Threads::Threads)
    engine_apply_options(engine_app_log_test)
    add_test(NAME engine.app_log COMMAND engine_app_log_test
                                         "${CMAKE_CURRENT_BINARY_DIR}/app-log-test-output")
    set_tests_properties(engine.app_log PROPERTIES TIMEOUT 30)
endif()

if(TARGET engine_resource)
    add_executable(engine_resource_read_test resource_read_test.cpp)
    target_link_libraries(engine_resource_read_test PRIVATE engine::resource)
    engine_apply_options(engine_resource_read_test)
    add_test(NAME engine.resource_read
             COMMAND engine_resource_read_test
                     "${CMAKE_CURRENT_BINARY_DIR}/resource-read-test-output")
    set_tests_properties(engine.resource_read PROPERTIES TIMEOUT 30)
endif()

if(TARGET engine_script)
    add_executable(engine_script_runtime_test script_runtime_test.cpp)
    target_link_libraries(
        engine_script_runtime_test PRIVATE engine::script engine::app engine::log engine::resource
                                           engine::world nlohmann_json::nlohmann_json)
    target_compile_definitions(engine_script_runtime_test PRIVATE JSON_NOEXCEPTION=1)
    engine_apply_options(engine_script_runtime_test)
    engine_copy_script_data(engine_script_runtime_test)
    add_test(NAME engine.script_runtime
             COMMAND engine_script_runtime_test
                     "${CMAKE_CURRENT_BINARY_DIR}/script-runtime-test-output")
    set_tests_properties(engine.script_runtime PROPERTIES TIMEOUT 60)

endif()

if(TARGET engine_asset_sync)
    add_executable(engine_asset_sync_test asset_sync_test.cpp)
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
    find_package(
        Python3
        COMPONENTS Interpreter
        REQUIRED)
    add_test(NAME engine.android_asset_packaging
             COMMAND "${Python3_EXECUTABLE}"
                     "${CMAKE_CURRENT_SOURCE_DIR}/prepare_android_assets_test.py")
    set_tests_properties(engine.android_asset_packaging PROPERTIES TIMEOUT 60)
    add_test(NAME engine.android_mcp
             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/android_mcp_test.py"
                     --mcp "$<TARGET_FILE:mcp_app>" --root "${PROJECT_SOURCE_DIR}")
    set_tests_properties(engine.android_mcp PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77)
    add_test(
        NAME engine.script_tooling
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/script_tooling_test.py" --mcp
                "$<TARGET_FILE:mcp_app>" --root "${PROJECT_SOURCE_DIR}" ${_script_tooling_options})
    set_tests_properties(engine.script_tooling PROPERTIES TIMEOUT 120)
endif()

if(TARGET engine_mcp)
    add_executable(engine_mcp_registry_test mcp_registry_test.cpp)
    target_link_libraries(engine_mcp_registry_test PRIVATE engine::mcp)
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
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/mcp_editor_transport_test.py"
                --mcp "$<TARGET_FILE:mcp_app>")
    set_tests_properties(editor.mcp_transport PROPERTIES TIMEOUT 60)
endif()

if(ENGINE_ENABLE_RUNTIME_DEPS
   OR ENGINE_ENABLE_EDITOR_DEPS
   OR ENGINE_ENABLE_ASSET_PIPELINE_DEPS
   OR ENGINE_ENABLE_V8)
    add_executable(engine_dependency_systems_test dependency_systems_test.cpp)
    target_link_libraries(engine_dependency_systems_test PRIVATE engine::core)
    target_compile_definitions(
        engine_dependency_systems_test
        PRIVATE ENGINE_TEST_PROJECT="${PROJECT_SOURCE_DIR}/projects/minimal"
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
    add_executable(engine_project_test project_system_test.cpp)
    target_link_libraries(engine_project_test PRIVATE engine::project)
    engine_apply_options(engine_project_test)
    add_test(NAME engine.project COMMAND engine_project_test
                                         "${CMAKE_CURRENT_BINARY_DIR}/project-test-output")
endif()

if(TARGET engine_modeling)
    foreach(_part core geometry)
        add_executable(engine_modeling_${_part}_test modeling_${_part}_test.cpp)
        target_link_libraries(engine_modeling_${_part}_test PRIVATE engine::modeling)
        engine_apply_options(engine_modeling_${_part}_test)
        add_test(NAME modeling.${_part} COMMAND engine_modeling_${_part}_test)
        set_tests_properties(modeling.${_part} PROPERTIES TIMEOUT 60)
    endforeach()
endif()

if(TARGET engine_modeling_editor)
    add_executable(engine_modeling_commands_test modeling_commands_test.cpp)
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

if(TARGET engine_asset_pipeline AND TARGET engine_modeling)
    add_executable(engine_modeling_pipeline_test modeling_pipeline_test.cpp)
    target_link_libraries(engine_modeling_pipeline_test
                          PRIVATE engine::modeling engine::asset_pipeline engine::resource)
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
            "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/mcp_modeling_test.py" --editor
            "$<TARGET_FILE:editor_app>" --mcp "$<TARGET_FILE:mcp_app>" --root
            "${PROJECT_SOURCE_DIR}")
    set_tests_properties(modeling.mcp PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE)
endif()

if(TARGET engine_editor)
    add_executable(engine_editor_commands_test editor_commands_test.cpp)
    target_include_directories(engine_editor_commands_test
                               PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    target_link_libraries(engine_editor_commands_test PRIVATE engine::editor engine::world
                                                              engine::resource)
    engine_apply_options(engine_editor_commands_test)
    add_test(NAME editor.commands COMMAND engine_editor_commands_test)
    set_tests_properties(editor.commands PROPERTIES TIMEOUT 30)
    add_executable(engine_editor_environment_test editor_environment_test.cpp)
    target_link_libraries(engine_editor_environment_test PRIVATE engine::editor engine::project
                                                                 engine::app engine::resource)
    engine_apply_options(engine_editor_environment_test)
    add_test(NAME engine.editor_environment COMMAND engine_editor_environment_test
                                                    "${CMAKE_CURRENT_BINARY_DIR}")
    add_executable(engine_editor_test editor_system_test.cpp)
    target_link_libraries(engine_editor_test PRIVATE engine::editor engine::project imgui::imgui)
    engine_apply_options(engine_editor_test)
    add_test(NAME engine.editor COMMAND engine_editor_test "${PROJECT_SOURCE_DIR}/projects/minimal")
endif()

if(TARGET editor_app)
    add_test(NAME editor.cli_help COMMAND editor_app --help)
    set_tests_properties(editor.cli_help PROPERTIES PASS_REGULAR_EXPRESSION "Usage: editor_app")
    add_test(NAME editor.cli_missing_project COMMAND editor_app)
    add_test(NAME editor.cli_invalid_project
             COMMAND editor_app --project "${CMAKE_CURRENT_BINARY_DIR}/missing-project" --frames 1)
    add_test(NAME editor.cli_invalid_frames
             COMMAND editor_app --project "${PROJECT_SOURCE_DIR}/projects/minimal" --frames invalid)
    set_tests_properties(editor.cli_missing_project editor.cli_invalid_project
                         editor.cli_invalid_frames PROPERTIES WILL_FAIL TRUE TIMEOUT 10)
endif()

if(ENGINE_ENABLE_RUNTIME_DEPS)
    add_executable(engine_render_foundation_test render_foundation_test.cpp)
    target_link_libraries(engine_render_foundation_test PRIVATE engine::world engine::resource)
    if(TARGET engine_resource_graph)
        target_link_libraries(engine_render_foundation_test PRIVATE engine::resource_graph)
        target_compile_definitions(engine_render_foundation_test
                                   PRIVATE ENGINE_TEST_RESOURCE_GRAPH=1)
    endif()
    engine_apply_options(engine_render_foundation_test)
    add_test(NAME engine.render_foundation COMMAND engine_render_foundation_test)
    set_tests_properties(engine.render_foundation PROPERTIES TIMEOUT 30)
    add_executable(engine_window_render_test window_render_test.cpp)
    target_link_libraries(
        engine_window_render_test
        PRIVATE engine::runtime
                engine::window
                engine::render
                engine::world
                engine::resource
                SDL3::SDL3
                bgfx::bgfx)
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
    add_executable(engine_task_test task_test.cpp)
    target_link_libraries(engine_task_test PRIVATE engine::task)
    engine_apply_options(engine_task_test)
    add_test(NAME editor.task COMMAND engine_task_test)
    set_tests_properties(editor.task PROPERTIES TIMEOUT 30)

    add_executable(engine_texture_pipeline_test texture_pipeline_test.cpp)
    target_link_libraries(
        engine_texture_pipeline_test PRIVATE engine::asset_pipeline engine::resource
                                             engine::scene_data basisu::basisu_encoder)
    target_compile_definitions(engine_texture_pipeline_test
                               PRIVATE ENGINE_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    engine_apply_options(engine_texture_pipeline_test)
    add_test(NAME editor.texture_pipeline COMMAND engine_texture_pipeline_test)
    set_tests_properties(editor.texture_pipeline PROPERTIES TIMEOUT 120)
    add_executable(engine_asset_pipeline_test asset_pipeline_test.cpp)
    target_include_directories(engine_asset_pipeline_test
                               PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    target_link_libraries(engine_asset_pipeline_test PRIVATE engine::asset_pipeline
                                                             engine::resource engine::scene_data)
    engine_apply_options(engine_asset_pipeline_test)
    add_test(NAME editor.asset_pipeline COMMAND engine_asset_pipeline_test)
    set_tests_properties(editor.asset_pipeline PROPERTIES TIMEOUT 90)

    if(TARGET engine_asset_build)
        add_executable(engine_asset_build_test asset_build_test.cpp)
        target_link_libraries(engine_asset_build_test
                              PRIVATE engine::asset_build engine::asset_pipeline engine::resource)
        engine_apply_options(engine_asset_build_test)
        add_test(NAME editor.asset_build COMMAND engine_asset_build_test)
        set_tests_properties(editor.asset_build PROPERTIES TIMEOUT 90)
    endif()

    if(TARGET asset_build_app AND ENGINE_NODE_EXECUTABLE)
        find_package(
            Python3
            COMPONENTS Interpreter
            REQUIRED)
        add_test(
            NAME editor.asset_pipeline_scripts
            COMMAND
                "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/asset_pipeline_script_test.py"
                --builder "$<TARGET_FILE:asset_build_app>" --project
                "${PROJECT_SOURCE_DIR}/projects/minimal")
        set_tests_properties(editor.asset_pipeline_scripts PROPERTIES TIMEOUT 180)
    endif()

    set_tests_properties(engine.dependencies
                         PROPERTIES ENVIRONMENT "PXR_PLUGINPATH_NAME=$<TARGET_FILE_DIR:usd>/usd")

    add_test(
        NAME engine.asset_tools
        COMMAND
            "${CMAKE_COMMAND}" "-DENGINE_SHADERC=${ENGINE_BGFX_SHADERC_EXECUTABLE}"
            "-DENGINE_TEXTUREC=${ENGINE_BGFX_TEXTUREC_EXECUTABLE}"
            "-DENGINE_GEOMETRYC=${ENGINE_BGFX_GEOMETRYC_EXECUTABLE}"
            "-DENGINE_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/asset-tools-output" -P
            "${CMAKE_CURRENT_SOURCE_DIR}/asset_tools_test.cmake")
    set_tests_properties(engine.asset_tools PROPERTIES TIMEOUT 100)

    add_executable(engine_tbb_test tbb_test.cpp)
    target_compile_features(engine_tbb_test PRIVATE cxx_std_20)
    target_link_libraries(engine_tbb_test PRIVATE TBB::tbb)
    engine_apply_options(engine_tbb_test)
    add_test(NAME engine.tbb COMMAND engine_tbb_test)
    set_tests_properties(engine.tbb PROPERTIES TIMEOUT 60)

    add_executable(engine_usd_test usd_test.cpp)
    target_compile_features(engine_usd_test PRIVATE cxx_std_20)
    target_link_libraries(engine_usd_test PRIVATE engine::core usdGeom)
    engine_apply_options(engine_usd_test)
    add_test(NAME engine.usd COMMAND engine_usd_test "${CMAKE_CURRENT_BINARY_DIR}/usd-test-output")
    set_tests_properties(engine.usd PROPERTIES TIMEOUT 60 ENVIRONMENT
                                               "PXR_PLUGINPATH_NAME=$<TARGET_FILE_DIR:usd>/usd")
endif()

if(ENGINE_ENABLE_V8)
    add_executable(engine_v8_test v8_test.cpp v8_smoke.cpp)
    target_link_libraries(engine_v8_test PRIVATE engine::v8)
    engine_apply_options(engine_v8_test)
    add_custom_command(
        TARGET engine_v8_test
        POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${ENGINE_V8_ICU_DATA}"
                "$<TARGET_FILE_DIR:engine_v8_test>/icudtl.dat"
        VERBATIM)
    add_test(NAME engine.v8 COMMAND engine_v8_test)
    set_tests_properties(engine.v8 PROPERTIES TIMEOUT 60)
endif()

if(TARGET engine_world)
    add_executable(engine_resource_catalog_test resource_catalog_test.cpp)
    target_link_libraries(engine_resource_catalog_test PRIVATE engine::world engine::resource)
    engine_apply_options(engine_resource_catalog_test)
    add_test(NAME engine.resource_catalog
             COMMAND engine_resource_catalog_test
                     "${CMAKE_CURRENT_BINARY_DIR}/resource-catalog-test-output")
    set_tests_properties(engine.resource_catalog PROPERTIES TIMEOUT 30)
    add_executable(engine_scene_test scene_test.cpp)
    target_link_libraries(engine_scene_test PRIVATE engine::world)
    engine_apply_options(engine_scene_test)
    add_test(NAME engine.scene COMMAND engine_scene_test)
    set_tests_properties(engine.scene PROPERTIES TIMEOUT 30)
    add_executable(engine_scene_data_test scene_data_test.cpp)
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
                "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scene_schema_test.py" --binary
                "$<TARGET_FILE:engine_scene_data_test>" --generated
                "${PROJECT_BINARY_DIR}/generated/typescript" --settings
                "${PROJECT_SOURCE_DIR}/projects/minimal/settings" --work-dir
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
            "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/editor_lifecycle_test.py" --editor
            "$<TARGET_FILE:editor_app>" --mcp "$<TARGET_FILE:mcp_app>" --root
            "${PROJECT_SOURCE_DIR}")
    set_tests_properties(editor.project_lifecycle PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
    add_test(
        NAME editor.mcp_commands
        COMMAND
            "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/mcp_editor_test.py" --editor
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
            "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/editor_without_script_test.py"
            --editor "$<TARGET_FILE:editor_app>" --mcp "$<TARGET_FILE:mcp_app>" --root
            "${PROJECT_SOURCE_DIR}")
    set_tests_properties(editor.without_script PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
endif()

if(TARGET engine_asset_pipeline)
    add_executable(engine_asset_metadata_test asset_metadata_test.cpp)
    target_include_directories(engine_asset_metadata_test
                               PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    target_link_libraries(engine_asset_metadata_test PRIVATE engine::asset_pipeline
                                                             engine::resource)
    engine_apply_options(engine_asset_metadata_test)
    add_test(NAME editor.asset_metadata COMMAND engine_asset_metadata_test)
endif()

if(TARGET engine_modeling_editor AND TARGET engine_asset_pipeline)
    add_executable(engine_scene_persistence_test scene_persistence_test.cpp)
    target_include_directories(engine_scene_persistence_test
                               PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    target_link_libraries(
        engine_scene_persistence_test PRIVATE engine::modeling_editor engine::editor
                                              engine::asset_pipeline engine::world engine::resource)
    engine_apply_options(engine_scene_persistence_test)
    add_test(NAME editor.scene_persistence COMMAND engine_scene_persistence_test)
endif()

if(TARGET engine_resource_graph)
    add_executable(engine_resource_graph_test resource_graph_test.cpp)
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
        "${CMAKE_CURRENT_SOURCE_DIR}/plugin_selection_test.cmake")
set_tests_properties(engine.plugin_selection PROPERTIES TIMEOUT 30)

if(TARGET engine_world)
    add_executable(engine_component_registry_test component_registry_test.cpp)
    target_link_libraries(engine_component_registry_test PRIVATE engine::world engine::resource)
    engine_apply_options(engine_component_registry_test)
    add_test(NAME engine.component_registry COMMAND engine_component_registry_test)
endif()

if(TARGET engine_resource)
    add_executable(engine_resource_codecs_test resource_codecs_test.cpp)
    target_link_libraries(engine_resource_codecs_test PRIVATE engine::resource)
    engine_apply_options(engine_resource_codecs_test)
    add_test(NAME resource.codecs COMMAND engine_resource_codecs_test)
endif()

if(TARGET engine_asset_pipeline)
    add_executable(engine_asset_importer_registry_test asset_importer_registry_test.cpp)
    target_link_libraries(engine_asset_importer_registry_test PRIVATE engine::asset_pipeline)
    target_include_directories(engine_asset_importer_registry_test
                               PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    engine_apply_options(engine_asset_importer_registry_test)
    add_test(NAME editor.asset_importer_registry COMMAND engine_asset_importer_registry_test)
endif()
