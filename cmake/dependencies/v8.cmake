include_guard(GLOBAL)

option(ENGINE_ENABLE_V8 "Enable explicitly prepared V8 monolith dependency" OFF)
if(NOT ENGINE_ENABLE_V8)
    return()
endif()
option(ENGINE_V8_ENABLE_WEBASSEMBLY "Enable V8 WebAssembly support" ON)
option(ENGINE_V8_ENABLE_I18N "Enable V8 internationalization support" ON)
option(ENGINE_V8_ENABLE_TEMPORAL "Enable V8 Temporal support" ON)
option(ENGINE_V8_ENABLE_SYSTEM_INSTRUMENTATION "Enable V8 system instrumentation" ON)
set(_engine_v8_features WEBASSEMBLY I18N TEMPORAL SYSTEM_INSTRUMENTATION)
set(_engine_v8_feature_bits "")
foreach(_feature IN LISTS _engine_v8_features)
    if(ENGINE_V8_ENABLE_${_feature})
        string(APPEND _engine_v8_feature_bits "1")
    else()
        string(APPEND _engine_v8_feature_bits "0")
    endif()
endforeach()
set(_engine_v8_feature_suffix "")
if(NOT _engine_v8_feature_bits STREQUAL "1111")
    set(_engine_v8_feature_suffix "-f${_engine_v8_feature_bits}")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The V8 preparation workflow requires a 64-bit target")
endif()
if(ANDROID)
    if(DEFINED ENGINE_V8_TARGET_OS AND NOT ENGINE_V8_TARGET_OS STREQUAL "android")
        message(FATAL_ERROR "The Android V8 target requires ENGINE_V8_TARGET_OS=android")
    endif()
    if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
        message(FATAL_ERROR "The Android V8 workflow currently supports ARM64 only")
    endif()
    if(NOT EXISTS "${CMAKE_ANDROID_NDK}/source.properties")
        message(FATAL_ERROR "Android V8 requires the configured NDK")
    endif()
    # NDK legacy toolchain 把 CMAKE_SYSTEM_VERSION 固定为 1；实际 ABI API 来自
    # ANDROID_PLATFORM_LEVEL。兼容 CMake 原生 Android 工具链并拒绝相互冲突的输入。
    set(_engine_v8_android_api "")
    set(_engine_v8_api_sources ANDROID_PLATFORM_LEVEL CMAKE_ANDROID_API ANDROID_PLATFORM)
    if(CMAKE_SYSTEM_VERSION MATCHES "^[0-9]+$" AND CMAKE_SYSTEM_VERSION GREATER 1)
        list(APPEND _engine_v8_api_sources CMAKE_SYSTEM_VERSION)
    endif()
    foreach(_api_source IN LISTS _engine_v8_api_sources)
        if(DEFINED ${_api_source} AND NOT "${${_api_source}}" STREQUAL "")
            if(NOT "${${_api_source}}" MATCHES "^(android-)?([0-9]+)$")
                message(
                    FATAL_ERROR
                        "Android V8 requires a numeric API: ${_api_source}=${${_api_source}}")
            endif()
            set(_api "${CMAKE_MATCH_2}")
            if(NOT _engine_v8_android_api STREQUAL "" AND NOT _engine_v8_android_api STREQUAL _api)
                message(
                    FATAL_ERROR
                        "Conflicting Android API levels: ${_api_source}=${_api}, expected ${_engine_v8_android_api}"
                )
            endif()
            set(_engine_v8_android_api "${_api}")
        endif()
    endforeach()
    if(_engine_v8_android_api STREQUAL "" OR _engine_v8_android_api LESS 28)
        message(FATAL_ERROR "Android V8 requires a configured Android API level >= 28")
    endif()
    if(CMAKE_CXX_COMPILER_TARGET MATCHES "android([0-9]+)$" AND NOT CMAKE_MATCH_1 STREQUAL
                                                                _engine_v8_android_api)
        message(
            FATAL_ERROR
                "V8 API ${_engine_v8_android_api} conflicts with compiler target ${CMAKE_CXX_COMPILER_TARGET}"
        )
    endif()
    if(DEFINED ENGINE_V8_ANDROID_API AND NOT ENGINE_V8_ANDROID_API STREQUAL _engine_v8_android_api)
        message(FATAL_ERROR "ENGINE_V8_ANDROID_API conflicts with the configured Android toolchain")
    endif()
    set(ENGINE_V8_TARGET_OS android)
    set(ENGINE_V8_ANDROID_NDK "${CMAKE_ANDROID_NDK}")
    set(ENGINE_V8_ANDROID_API "${_engine_v8_android_api}")
    set(_engine_v8_target_cpu arm64)
    set(_engine_v8_default_root "${PROJECT_SOURCE_DIR}/build/v8/android-arm64")
elseif(CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "Only Android ARM64 cross-compilation is supported by the V8 workflow")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64|x64)$")
    set(_engine_v8_target_cpu x64)
elseif(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
    set(_engine_v8_target_cpu arm64)
else()
    message(
        FATAL_ERROR "The V8 preparation workflow supports native x64 and macOS ARM64 builds only")
endif()
if(APPLE AND CMAKE_OSX_ARCHITECTURES)
    if(_engine_v8_target_cpu STREQUAL "x64")
        set(_engine_v8_osx_architecture x86_64)
    else()
        set(_engine_v8_osx_architecture arm64)
    endif()
    if(NOT CMAKE_OSX_ARCHITECTURES STREQUAL _engine_v8_osx_architecture)
        message(
            FATAL_ERROR "V8 requires one native macOS architecture: ${_engine_v8_osx_architecture}")
    endif()
endif()
if(WIN32 AND NOT MSVC)
    message(FATAL_ERROR "V8 on Windows requires an MSVC-compatible compiler and runtime")
endif()
if(WIN32
   AND DEFINED CMAKE_CXX_COMPILER_ARCHITECTURE_ID
   AND NOT CMAKE_CXX_COMPILER_ARCHITECTURE_ID MATCHES "^(x64|X64|AMD64|amd64)$")
    message(FATAL_ERROR "V8 requires an x64 compiler target on Windows")
endif()
if(NOT ANDROID)
    set(_engine_v8_default_root "${PROJECT_SOURCE_DIR}/build/v8")
endif()
set(ENGINE_V8_BUILD_ROOT
    "${_engine_v8_default_root}"
    CACHE PATH "Ignored V8 workspace and toolchain directory")
cmake_path(ABSOLUTE_PATH ENGINE_V8_BUILD_ROOT NORMALIZE)
set(_engine_v8_allowed_root "${PROJECT_SOURCE_DIR}/build")
cmake_path(IS_PREFIX _engine_v8_allowed_root "${ENGINE_V8_BUILD_ROOT}" NORMALIZE
           _engine_v8_inside_build)
if(NOT _engine_v8_inside_build OR ENGINE_V8_BUILD_ROOT STREQUAL _engine_v8_allowed_root)
    message(FATAL_ERROR "ENGINE_V8_BUILD_ROOT must be inside the repository build directory")
endif()
if(EXISTS "${ENGINE_V8_BUILD_ROOT}")
    file(REAL_PATH "${ENGINE_V8_BUILD_ROOT}" _engine_v8_resolved_root)
    file(REAL_PATH "${_engine_v8_allowed_root}" _engine_v8_resolved_allowed)
    cmake_path(IS_PREFIX _engine_v8_resolved_allowed "${_engine_v8_resolved_root}" NORMALIZE
               _engine_v8_inside_real_build)
    if(NOT _engine_v8_inside_real_build OR _engine_v8_resolved_root STREQUAL
                                           _engine_v8_resolved_allowed)
        message(FATAL_ERROR "Resolved V8 build root escapes the repository build directory")
    endif()
endif()
set(ENGINE_V8_JOBS
    4
    CACHE STRING "Parallel jobs for explicit V8 prepare/build targets")
set(_engine_v8_script "${PROJECT_SOURCE_DIR}/cmake/dependencies/prepare_v8.cmake")
set(_engine_v8_config "$<IF:$<CONFIG:Debug>,Debug,Release>")
set(_engine_v8_source "${ENGINE_V8_BUILD_ROOT}/source/v8")
# 同一主机工具链的清单内容跨 preset 一致；仅本机构建目录保存绝对路径。
set(_engine_v8_host_file "${CMAKE_CURRENT_BINARY_DIR}/v8-host-toolchain.txt")
if(ANDROID)
    file(
        CONFIGURE
        OUTPUT
        "${_engine_v8_host_file}"
        CONTENT
        "host_system=@CMAKE_HOST_SYSTEM_NAME@
host_processor=@CMAKE_HOST_SYSTEM_PROCESSOR@
target_system=@CMAKE_SYSTEM_NAME@
target_processor=@CMAKE_SYSTEM_PROCESSOR@
target_compiler=@CMAKE_CXX_COMPILER@
target_compiler_id=@CMAKE_CXX_COMPILER_ID@
target_compiler_version=@CMAKE_CXX_COMPILER_VERSION@
android_ndk=@ENGINE_V8_ANDROID_NDK@
android_api=@ENGINE_V8_ANDROID_API@
"
        @ONLY)
else()
    file(
        CONFIGURE
        OUTPUT
        "${_engine_v8_host_file}"
        CONTENT
        "compiler=@CMAKE_CXX_COMPILER@
compiler_id=@CMAKE_CXX_COMPILER_ID@
compiler_version=@CMAKE_CXX_COMPILER_VERSION@
system=@CMAKE_SYSTEM_NAME@
processor=@CMAKE_SYSTEM_PROCESSOR@
osx_sysroot=@CMAKE_OSX_SYSROOT@
osx_deployment=@CMAKE_OSX_DEPLOYMENT_TARGET@
vs_instance=@CMAKE_GENERATOR_INSTANCE@
vs_toolset=@CMAKE_VS_PLATFORM_TOOLSET_VERSION@
windows_sdk=@CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION@
"
        @ONLY)
endif()
if(WIN32)
    set(_engine_v8_library engine_v8.lib)
    set(_engine_v8_windows_crt dynamic)
    if(DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
        if(CMAKE_MSVC_RUNTIME_LIBRARY STREQUAL "MultiThreaded$<$<CONFIG:Debug>:Debug>")
            set(_engine_v8_windows_crt static)
        elseif(NOT CMAKE_MSVC_RUNTIME_LIBRARY STREQUAL "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
            message(FATAL_ERROR "V8 requires the standard per-config MSVC runtime selection")
        endif()
    endif()
else()
    set(_engine_v8_library libengine_v8.a)
endif()
set(_engine_v8_variant "$<IF:$<CONFIG:Debug>,debug,release>")
if(WIN32 AND _engine_v8_windows_crt STREQUAL "static")
    set(_engine_v8_variant "mt-${_engine_v8_variant}")
endif()
string(APPEND _engine_v8_variant "${_engine_v8_feature_suffix}")
set(_engine_v8_arguments
    "-DENGINE_V8_BUILD_ROOT=${ENGINE_V8_BUILD_ROOT}" "-DENGINE_V8_CONFIG=${_engine_v8_config}"
    "-DENGINE_V8_TARGET_CPU=${_engine_v8_target_cpu}" "-DENGINE_V8_JOBS=${ENGINE_V8_JOBS}"
    "-DENGINE_V8_HOST_TOOLCHAIN_FILE=${_engine_v8_host_file}")
foreach(_feature IN LISTS _engine_v8_features)
    list(APPEND _engine_v8_arguments
         "-DENGINE_V8_ENABLE_${_feature}=${ENGINE_V8_ENABLE_${_feature}}")
endforeach()
if(ANDROID)
    list(APPEND _engine_v8_arguments "-DENGINE_V8_TARGET_OS=android"
         "-DENGINE_V8_ANDROID_NDK=${ENGINE_V8_ANDROID_NDK}"
         "-DENGINE_V8_ANDROID_API=${ENGINE_V8_ANDROID_API}")
else()
    list(APPEND _engine_v8_arguments "-DENGINE_V8_HOST_COMPILER=${CMAKE_CXX_COMPILER}")
endif()
if(WIN32)
    list(APPEND _engine_v8_arguments "-DENGINE_V8_WINDOWS_CRT=${_engine_v8_windows_crt}")
endif()

# 准备目标包含网络操作，必须单独显式运行，不属于默认构建。
add_custom_target(
    engine_v8_prepare
    COMMAND "${CMAKE_COMMAND}" ${_engine_v8_arguments} -DENGINE_V8_ACTION=PREPARE -P
            "${_engine_v8_script}"
    USES_TERMINAL VERBATIM)
# 每次只做轻量身份/产物检查；成功缓存命中时不启动 GN、Ninja 或归档器。
# 入口保留为目标，确保跨 preset 共享产物缺失、工具链变化也能在构建时被检测。
# BUILD 不隐式下载依赖；未准备时给出明确错误。
set(_engine_v8_byproducts
    "${_engine_v8_source}/out/engine.${_engine_v8_variant}/obj/${_engine_v8_library}"
    "${ENGINE_V8_BUILD_ROOT}/include/${_engine_v8_variant}/v8-gn.h"
    "${_engine_v8_source}/out/engine.${_engine_v8_variant}/.engine-v8-build.identity")
if(ENGINE_V8_ENABLE_I18N)
    list(APPEND _engine_v8_byproducts
         "${_engine_v8_source}/out/engine.${_engine_v8_variant}/icudtl.dat")
endif()
add_custom_target(
    engine_v8_build
    COMMAND "${CMAKE_COMMAND}" ${_engine_v8_arguments} -DENGINE_V8_ACTION=BUILD -P
            "${_engine_v8_script}"
    BYPRODUCTS ${_engine_v8_byproducts}
    USES_TERMINAL VERBATIM)

add_library(engine_v8 STATIC IMPORTED GLOBAL)
add_library(engine::v8 ALIAS engine_v8)
set_property(TARGET engine_v8 PROPERTY IMPORTED_CONFIGURATIONS Debug Release)
set_property(TARGET engine_v8 PROPERTY INTERFACE_COMPILE_FEATURES cxx_std_20)
set_property(
    TARGET engine_v8
    PROPERTY
        INTERFACE_COMPILE_DEFINITIONS
        "V8_GN_HEADER;ENGINE_V8_HAS_WEBASSEMBLY=$<BOOL:${ENGINE_V8_ENABLE_WEBASSEMBLY}>;ENGINE_V8_HAS_I18N=$<BOOL:${ENGINE_V8_ENABLE_I18N}>;ENGINE_V8_HAS_TEMPORAL=$<BOOL:${ENGINE_V8_ENABLE_TEMPORAL}>;ENGINE_V8_HAS_SYSTEM_INSTRUMENTATION=$<BOOL:${ENGINE_V8_ENABLE_SYSTEM_INSTRUMENTATION}>"
)
set_property(TARGET engine_v8 PROPERTY INTERFACE_COMPILE_OPTIONS
                                       "$<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>")
foreach(_configuration IN ITEMS Debug Release)
    string(TOLOWER "${_configuration}" _lower)
    if(WIN32 AND _engine_v8_windows_crt STREQUAL "static")
        set(_lower "mt-${_lower}")
    endif()
    string(APPEND _lower "${_engine_v8_feature_suffix}")
    string(TOUPPER "${_configuration}" _upper)
    set(_output "${_engine_v8_source}/out/engine.${_lower}")
    # include 副本独立于 Git 工作目录，允许先配置再显式准备。
    file(MAKE_DIRECTORY "${ENGINE_V8_BUILD_ROOT}/include/${_lower}")
    set_property(TARGET engine_v8 PROPERTY "IMPORTED_LOCATION_${_upper}"
                                           "${_output}/obj/${_engine_v8_library}")
endforeach()
set_property(TARGET engine_v8 PROPERTY MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release)
set_property(TARGET engine_v8 PROPERTY MAP_IMPORTED_CONFIG_MINSIZEREL Release)
file(MAKE_DIRECTORY "${ENGINE_V8_BUILD_ROOT}/include/v8")
set_property(
    TARGET engine_v8
    PROPERTY
        INTERFACE_INCLUDE_DIRECTORIES
        "${ENGINE_V8_BUILD_ROOT}/include/v8;${ENGINE_V8_BUILD_ROOT}/include/${_engine_v8_variant}")
# ICU 数据不在 monolith 中；最终使用者部署时需将其复制到程序旁。
set(_engine_v8_icu_data "")
if(ENGINE_V8_ENABLE_I18N)
    set(_engine_v8_icu_data "${_engine_v8_source}/out/engine.${_engine_v8_variant}/icudtl.dat")
endif()
set(ENGINE_V8_ICU_DATA
    "${_engine_v8_icu_data}"
    CACHE INTERNAL "V8 ICU data to deploy beside a consuming executable" FORCE)
set_property(TARGET engine_v8 PROPERTY ENGINE_V8_ICU_DATA "${ENGINE_V8_ICU_DATA}")
if(ANDROID)
    # Android 的线程 API 在 libc 中；Rust/Temporal 的系统调用依赖随目标传递。
    set_property(TARGET engine_v8 PROPERTY INTERFACE_LINK_LIBRARIES "android;log;dl;m")
elseif(WIN32)
    # Rust std 的原生链接依赖不会随 rlib 自动传递给最终 C++ 链接器。
    set_property(
        TARGET engine_v8
        PROPERTY
            INTERFACE_LINK_LIBRARIES
            "dbghelp;winmm;ws2_32;advapi32;user32;bcrypt;ntdll;synchronization;userenv;legacy_stdio_definitions"
    )
elseif(APPLE)
    # 与 GN 嵌入样例一致，传递 Darwin 时间、系统信息与安全 API 的框架依赖。
    find_library(_engine_v8_foundation Foundation REQUIRED)
    find_library(_engine_v8_core_foundation CoreFoundation REQUIRED)
    find_library(_engine_v8_security Security REQUIRED)
    set_property(
        TARGET engine_v8
        PROPERTY INTERFACE_LINK_LIBRARIES
                 "${_engine_v8_foundation};${_engine_v8_core_foundation};${_engine_v8_security}")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(Threads REQUIRED)
    set_property(TARGET engine_v8 PROPERTY INTERFACE_LINK_LIBRARIES
                                           "Threads::Threads;${CMAKE_DL_LIBS};rt")
endif()
add_dependencies(engine_v8 engine_v8_build)
