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
