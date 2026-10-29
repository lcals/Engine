if(ENGINE_ENABLE_RUNTIME_DEPS)
    if(ANDROID)
        add_library(engine_platform STATIC platforms/android/native/app.cpp)
    else()
        add_library(engine_platform STATIC platforms/desktop/app.cpp)
    endif()
    add_library(engine::app ALIAS engine_platform)
    target_compile_features(engine_platform PUBLIC cxx_std_20)
    target_link_libraries(
        engine_platform
        PUBLIC engine::core
        PRIVATE SDL3::SDL3)
    engine_apply_options(engine_platform)

endif()

function(engine_configure_platform_graphics)
    if(ANDROID)
        target_sources(engine_window PRIVATE platforms/android/native/android_surface.cpp)
        target_include_directories(engine_window PRIVATE "${PROJECT_SOURCE_DIR}/platforms")
        target_include_directories(engine_render PRIVATE "${PROJECT_SOURCE_DIR}/platforms")
        target_link_libraries(engine_render PRIVATE android)
    endif()
endfunction()
