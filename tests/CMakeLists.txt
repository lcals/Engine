add_executable(engine_tests greeting_test.cpp)
target_link_libraries(engine_tests PRIVATE engine::core)
engine_apply_options(engine_tests)
add_test(NAME engine.unit COMMAND engine_tests)

if(ENGINE_BUILD_APP)
    add_test(NAME engine.cli COMMAND engine_app CTest)
    set_tests_properties(engine.cli PROPERTIES PASS_REGULAR_EXPRESSION "Hello, CTest!")
    add_test(NAME engine.cli_default COMMAND engine_app)
    set_tests_properties(engine.cli_default PROPERTIES PASS_REGULAR_EXPRESSION "Hello, World!")
endif()

if(ENGINE_ENABLE_ASSET_PIPELINE_DEPS)
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
