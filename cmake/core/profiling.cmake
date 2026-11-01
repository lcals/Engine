# Probes are private to explicitly selected implementation targets.
function(engine_enable_profiling target)
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    if(ENGINE_ENABLE_TRACY)
        target_compile_definitions(${target} PRIVATE ENGINE_ENABLE_TRACY=1)
        target_link_libraries(${target} PRIVATE Tracy::TracyClient)
    endif()
endfunction()
