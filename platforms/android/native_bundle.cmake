# Gradle 只负责 Java 与 APK，原生构建继续使用根 CMake Presets。
if(NOT ANDROID_ABI STREQUAL "arm64-v8a")
    message(FATAL_ERROR "The current Android APK preset requires arm64-v8a")
endif()
if(NOT ANDROID_STL STREQUAL "c++_static")
    message(FATAL_ERROR "The Android application requires c++_static for its single native library")
endif()
get_target_property(_engine_android_sdl_type SDL3::SDL3 TYPE)
if(NOT _engine_android_sdl_type STREQUAL "STATIC_LIBRARY")
    message(FATAL_ERROR "The Android application requires a static SDL3 library")
endif()
set(ENGINE_ANDROID_PROJECT
    "${PROJECT_SOURCE_DIR}/projects/showcase"
    CACHE PATH "Project whose compiled assets are packaged in the Android application")
set(ENGINE_ANDROID_ASSET_BUILDER
    ""
    CACHE
        FILEPATH
        "Host asset_build_app used to compile GUID assets for Android; empty reuses an Android catalog"
)
set(_engine_android_asset_builder_arguments)
if(ENGINE_ANDROID_ASSET_BUILDER)
    if(NOT EXISTS "${ENGINE_ANDROID_ASSET_BUILDER}" OR IS_DIRECTORY
                                                       "${ENGINE_ANDROID_ASSET_BUILDER}")
        message(
            FATAL_ERROR "ENGINE_ANDROID_ASSET_BUILDER must name an existing host asset_build_app")
    endif()
    list(APPEND _engine_android_asset_builder_arguments --asset-builder
         "${ENGINE_ANDROID_ASSET_BUILDER}")
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
target_compile_definitions(engine_app
                           PRIVATE ENGINE_START_PACKAGED_GAMEPLAY=$<TARGET_EXISTS:engine_script>)
set(_engine_android_script_backend none)
set(_engine_android_has_icu false)
set(_engine_android_icu_commands)
if(TARGET engine_script)
    set(_engine_android_script_backend "${ENGINE_SCRIPT_BACKEND}")
endif()
if(TARGET engine_script AND ENGINE_SCRIPT_BACKEND STREQUAL "v8")
    target_sources(
        engine_app PRIVATE "${PROJECT_SOURCE_DIR}/tests/v8_smoke.cpp"
                           "${PROJECT_SOURCE_DIR}/platforms/android/native/android_v8_smoke.cpp")
    target_include_directories(engine_app PRIVATE "${PROJECT_SOURCE_DIR}/tests")
    target_link_libraries(engine_app PRIVATE engine::v8 log)
    if(ENGINE_V8_ICU_DATA)
        set(_engine_android_has_icu true)
        list(
            APPEND
            _engine_android_icu_commands
            COMMAND
            "${CMAKE_COMMAND}"
            -E
            make_directory
            "${CMAKE_CURRENT_BINARY_DIR}/android/assets/v8"
            COMMAND
            "${CMAKE_COMMAND}"
            -E
            copy_if_different
            "${ENGINE_V8_ICU_DATA}"
            "${CMAKE_CURRENT_BINARY_DIR}/android/assets/v8/icudtl.dat")
    endif()
endif()
if(NOT _engine_android_has_icu)
    list(
        APPEND
        _engine_android_icu_commands
        COMMAND
        "${CMAKE_COMMAND}"
        -E
        rm
        -f
        "${CMAKE_CURRENT_BINARY_DIR}/android/assets/v8/icudtl.dat")
endif()
file(
    GENERATE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/android/script-runtime.properties"
    CONTENT "backend=${_engine_android_script_backend}\nicu=${_engine_android_has_icu}\n")
# 保存实际链接输入，便于验证所选 VM 和唯一原生库。
target_link_options(engine_app PRIVATE "LINKER:-Map,${CMAKE_CURRENT_BINARY_DIR}/engine_app.map")
# SDL 的 JNI 注册由 Java 虚拟机调用，静态链接时显式保留其入口。
target_link_options(engine_app PRIVATE "LINKER:--undefined=JNI_OnLoad")
set(_engine_android_lib_dir "${CMAKE_CURRENT_BINARY_DIR}/android/jniLibs/${ANDROID_ABI}")
set(_engine_sdl_java "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/sdl3/java")
if(NOT EXISTS "${_engine_sdl_java}/org/libsdl/app/SDLActivity.java")
    message(FATAL_ERROR "The SDL3 Android Java sources are missing; reinstall the SDL3 overlay")
endif()
file(GLOB _engine_sdl_java_sources CONFIGURE_DEPENDS "${_engine_sdl_java}/org/libsdl/app/*.java")
set(_engine_android_java_dir "${CMAKE_CURRENT_BINARY_DIR}/android/java/org/libsdl/app")

add_custom_target(
    engine_android_bundle
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_engine_android_lib_dir}"
    # 只清理旧打包布局留下的生成副本，保留依赖及所有编译缓存。
    COMMAND "${CMAKE_COMMAND}" -E rm -f "${_engine_android_lib_dir}/libSDL3.so"
            "${_engine_android_lib_dir}/libc++_shared.so"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:engine_app>"
            "${_engine_android_lib_dir}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_engine_android_java_dir}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${_engine_sdl_java_sources}
            "${_engine_android_java_dir}" ${_engine_android_icu_commands}
    COMMAND
        "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/prepare_android_assets.py" --project
        "${ENGINE_ANDROID_PROJECT}" --output "${CMAKE_CURRENT_BINARY_DIR}/android/assets"
        ${_engine_android_asset_builder_arguments}
    DEPENDS engine_app
    COMMENT "Preparing Android library, SDL3 Java, selected script runtime and project assets"
    VERBATIM)
