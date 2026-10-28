# Gradle 只负责 Java 与 APK，原生构建继续使用根 CMake Presets。
if(NOT ANDROID_ABI STREQUAL "arm64-v8a")
    message(FATAL_ERROR "The current Android APK preset requires arm64-v8a")
endif()
set(_engine_android_lib_dir "${CMAKE_CURRENT_BINARY_DIR}/android/jniLibs/${ANDROID_ABI}")
set(_engine_android_stl "${CMAKE_SYSROOT}/usr/lib/aarch64-linux-android/libc++_shared.so")
if(NOT EXISTS "${_engine_android_stl}")
    message(FATAL_ERROR "Android shared C++ runtime is missing: ${_engine_android_stl}")
endif()
set(_engine_sdl_java "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/sdl3/java")
if(NOT EXISTS "${_engine_sdl_java}/org/libsdl/app/SDLActivity.java")
    message(FATAL_ERROR "The SDL3 Android Java sources are missing; reinstall the SDL3 overlay")
endif()
file(GLOB _engine_sdl_java_sources CONFIGURE_DEPENDS "${_engine_sdl_java}/org/libsdl/app/*.java")
set(_engine_android_java_dir "${CMAKE_CURRENT_BINARY_DIR}/android/java/org/libsdl/app")

add_custom_target(engine_android_bundle
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_engine_android_lib_dir}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:engine_app>" "$<TARGET_FILE:SDL3::SDL3>" "${_engine_android_stl}"
        "${_engine_android_lib_dir}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_engine_android_java_dir}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        ${_engine_sdl_java_sources} "${_engine_android_java_dir}"
    DEPENDS engine_app
    COMMENT "Preparing Android native libraries and matching SDL3 Java sources"
    VERBATIM)
