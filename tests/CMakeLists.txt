add_executable(engine_system_test system_test.cpp)
target_link_libraries(engine_system_test PRIVATE engine::core)
engine_apply_options(engine_system_test)
add_test(NAME engine.system COMMAND engine_system_test)

add_test(NAME engine.v8_cache
    COMMAND "${CMAKE_COMMAND}"
        "-DENGINE_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/v8-cache-test"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/v8_cache_test.cmake")
set_tests_properties(engine.v8_cache PROPERTIES TIMEOUT 30)

if(ENGINE_ENABLE_RUNTIME_DEPS OR ENGINE_ENABLE_ASSET_PIPELINE_DEPS OR ENGINE_ENABLE_V8)
    add_executable(engine_dependency_systems_test dependency_systems_test.cpp)
    target_link_libraries(engine_dependency_systems_test PRIVATE engine::core)
    target_compile_definitions(engine_dependency_systems_test PRIVATE
        ENGINE_TEST_PROJECT="${PROJECT_SOURCE_DIR}/projects/minimal"
        ENGINE_HAS_RUNTIME=$<BOOL:${ENGINE_ENABLE_RUNTIME_DEPS}>
        ENGINE_HAS_EDITOR=$<BOOL:${ENGINE_ENABLE_EDITOR_DEPS}>
        ENGINE_HAS_ASSET_PIPELINE=$<BOOL:${ENGINE_ENABLE_ASSET_PIPELINE_DEPS}>
        ENGINE_HAS_V8=$<BOOL:${ENGINE_ENABLE_V8}>)
    if(ENGINE_ENABLE_RUNTIME_DEPS)
        target_link_libraries(engine_dependency_systems_test PRIVATE ${ENGINE_RUNTIME_SYSTEM_TARGETS})
    endif()
    if(ENGINE_ENABLE_EDITOR_DEPS)
        target_link_libraries(engine_dependency_systems_test PRIVATE engine::editor)
    endif()
    if(ENGINE_ENABLE_ASSET_PIPELINE_DEPS)
        target_link_libraries(engine_dependency_systems_test PRIVATE engine::asset_pipeline)
    endif()
    if(ENGINE_ENABLE_V8)
        target_link_libraries(engine_dependency_systems_test PRIVATE engine::script)
    endif()
    engine_apply_options(engine_dependency_systems_test)
    add_test(NAME engine.dependencies COMMAND engine_dependency_systems_test)
    set_tests_properties(engine.dependencies PROPERTIES TIMEOUT 60)
endif()

if(TARGET engine_app)
    add_test(NAME engine.cli_help COMMAND engine_app --help)
    set_tests_properties(engine.cli_help PROPERTIES PASS_REGULAR_EXPRESSION "Usage: engine_app")
    add_test(NAME engine.cli_missing_project COMMAND engine_app)
    add_test(NAME engine.cli_invalid_project COMMAND engine_app
        --project "${CMAKE_CURRENT_BINARY_DIR}/missing-project" --frames 1)
    add_test(NAME engine.cli_invalid_frames COMMAND engine_app --project test --frames invalid)
    set_tests_properties(engine.cli_missing_project engine.cli_invalid_frames
        engine.cli_invalid_project PROPERTIES WILL_FAIL TRUE)
endif()

if(ENGINE_ENABLE_RUNTIME_DEPS)
    add_executable(engine_project_test project_system_test.cpp)
    target_link_libraries(engine_project_test PRIVATE engine::project)
    engine_apply_options(engine_project_test)
    add_test(NAME engine.project COMMAND engine_project_test
        "${CMAKE_CURRENT_BINARY_DIR}/project-test-output")

    add_executable(engine_window_render_test window_render_test.cpp)
    target_link_libraries(engine_window_render_test PRIVATE engine::runtime engine::window
        engine::render SDL3::SDL3 bgfx::bgfx)
    target_compile_definitions(engine_window_render_test PRIVATE
        ENGINE_TEST_PROJECT="${PROJECT_SOURCE_DIR}/projects/minimal")
    engine_apply_options(engine_window_render_test)
    if(ENGINE_ENABLE_GPU_TESTS)
        add_test(NAME engine.window_render COMMAND engine_window_render_test)
        set_tests_properties(engine.window_render PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
    endif()
endif()

if(ENGINE_ENABLE_ASSET_PIPELINE_DEPS)
    set_tests_properties(engine.dependencies PROPERTIES
        ENVIRONMENT "PXR_PLUGINPATH_NAME=$<TARGET_FILE_DIR:usd>/usd")

    add_test(NAME engine.asset_tools
        COMMAND "${CMAKE_COMMAND}"
            "-DENGINE_SHADERC=${ENGINE_BGFX_SHADERC_EXECUTABLE}"
            "-DENGINE_TEXTUREC=${ENGINE_BGFX_TEXTUREC_EXECUTABLE}"
            "-DENGINE_GEOMETRYC=${ENGINE_BGFX_GEOMETRYC_EXECUTABLE}"
            "-DENGINE_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/asset-tools-output"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/asset_tools_test.cmake")
    set_tests_properties(engine.asset_tools PROPERTIES TIMEOUT 100)

    add_executable(engine_tbb_test tbb_test.cpp)
    target_compile_features(engine_tbb_test PRIVATE cxx_std_20)
    target_link_libraries(engine_tbb_test PRIVATE TBB::tbb)
    engine_apply_options(engine_tbb_test)
    add_test(NAME engine.tbb COMMAND engine_tbb_test)
    set_tests_properties(engine.tbb PROPERTIES TIMEOUT 60)

    add_executable(engine_usd_test usd_test.cpp)
    target_compile_features(engine_usd_test PRIVATE cxx_std_20)
    target_link_libraries(engine_usd_test PRIVATE usdGeom)
    engine_apply_options(engine_usd_test)
    add_test(NAME engine.usd COMMAND engine_usd_test "${CMAKE_CURRENT_BINARY_DIR}/usd-test-output")
    set_tests_properties(engine.usd PROPERTIES
        TIMEOUT 60
        ENVIRONMENT "PXR_PLUGINPATH_NAME=$<TARGET_FILE_DIR:usd>/usd")
endif()

if(ENGINE_ENABLE_V8)
    add_executable(engine_v8_test v8_test.cpp)
    target_link_libraries(engine_v8_test PRIVATE engine::v8)
    engine_apply_options(engine_v8_test)
    add_custom_command(TARGET engine_v8_test POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${ENGINE_V8_ICU_DATA}" "$<TARGET_FILE_DIR:engine_v8_test>/icudtl.dat"
        VERBATIM)
    add_test(NAME engine.v8 COMMAND engine_v8_test)
    set_tests_properties(engine.v8 PROPERTIES TIMEOUT 60)
endif()
