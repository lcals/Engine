include_guard(GLOBAL)

function(engine_apply_platform_options target)
    get_target_property(_engine_target_type ${target} TYPE)
    if(WIN32
       AND MSVC
       AND _engine_target_type STREQUAL "EXECUTABLE")
        target_sources(${target} PRIVATE "${PROJECT_SOURCE_DIR}/platforms/windows/diagnostics.cpp"
                                         "${PROJECT_SOURCE_DIR}/platforms/windows/utf8.manifest")
    endif()
endfunction()
