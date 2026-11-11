# Effekseer 只供选中的特效插件使用；bgfx 继续拥有唯一图形后端。
include_guard(GLOBAL)
if(NOT "runtime/effects" IN_LIST ENGINE_SELECTED_PLUGINS)
    return()
endif()

find_package(Git REQUIRED)
set(_effects_root "${PROJECT_SOURCE_DIR}/third_party")
set(_effekseer_source "${_effects_root}/effekseer")
set(_efkbgfx_source "${_effects_root}/efkbgfx")
if(NOT EXISTS "${_effekseer_source}/Dev/Cpp/Effekseer/CMakeLists.txt"
   OR NOT EXISTS "${_efkbgfx_source}/renderer/bgfxrenderer.cpp")
    message(
        FATAL_ERROR
            "Effects dependencies are missing; run git submodule update --init third_party/effekseer third_party/efkbgfx"
    )
endif()

# 上游 CMake 仍管理其源码列表；配置在函数作用域内，不改变宿主的后端、工具或部署设置。
function(_engine_configure_effekseer source)
    set(CLANG_FORMAT_ENABLED OFF)
    set(USE_LIBPNG_LOADER OFF)
    set(NETWORK_ENABLED OFF)
    set(BUILD_WITH_EASY_PROFILER OFF)
    # 只加载 runtime/common 的官方 CMake，不创建第二套窗口、音频或渲染后端。
    include("${source}/cmake/FilterFolder.cmake")
    function(WarnError target)
        if(MSVC)
            target_compile_options(
                ${target}
                PRIVATE /permissive-
                        /utf-8
                        /we4267
                        /we4456
                        /we4458
                        /we4715
                        /we4302
                        /we4311
                        /we4312)
        elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
            target_compile_options(${target} PRIVATE -Werror=return-type -Werror=conversion-null
                                                     -Werror=int-to-pointer-cast)
        elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            target_compile_options(${target} PRIVATE -Werror=return-type -Werror=null-conversion
                                                     -Werror=int-to-pointer-cast)
        endif()
    endfunction()
    set(EFK_THIRDPARTY_INCLUDES "${source}/Dev/Cpp/3rdParty/flatbuffers/include")
    add_subdirectory("${source}/Dev/Cpp/Effekseer" "${CMAKE_CURRENT_BINARY_DIR}/effekseer/runtime"
                     EXCLUDE_FROM_ALL SYSTEM)
    add_subdirectory(
        "${source}/Dev/Cpp/EffekseerRendererCommon"
        "${CMAKE_CURRENT_BINARY_DIR}/effekseer/renderer_common" EXCLUDE_FROM_ALL SYSTEM)
    foreach(_target Effekseer EffekseerRendererCommon)
        target_compile_features(${_target} PRIVATE cxx_std_17)
        set_target_properties(${_target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
        engine_apply_compiler_cache(${_target})
    endforeach()
endfunction()
_engine_configure_effekseer("${_effekseer_source}")

# 兼容补丁只应用到构建目录。源码和补丁未变时不触碰副本，保留增量缓存。
function(_engine_stage_effects_source name source patch output)
    set(_staged "${CMAKE_CURRENT_BINARY_DIR}/source/${name}")
    set(_inputs "${patch}")
    foreach(_relative IN LISTS ARGN)
        list(APPEND _inputs "${source}/${_relative}")
    endforeach()
    set_property(
        DIRECTORY
        APPEND
        PROPERTY CMAKE_CONFIGURE_DEPENDS ${_inputs})
    set(_fingerprint "")
    foreach(_input IN LISTS _inputs)
        file(SHA256 "${_input}" _hash)
        string(APPEND _fingerprint "${_hash}\n")
    endforeach()
    set(_stamp "${_staged}/.engine-patch-inputs")
    set(_existing "")
    if(EXISTS "${_stamp}")
        file(READ "${_stamp}" _existing)
    endif()
    foreach(_relative IN LISTS ARGN)
        if(NOT EXISTS "${_staged}/${_relative}")
            set(_existing "")
        endif()
    endforeach()
    if(NOT _existing STREQUAL _fingerprint)
        foreach(_relative IN LISTS ARGN)
            configure_file("${source}/${_relative}" "${_staged}/${_relative}" COPYONLY)
        endforeach()
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" apply --no-index "${patch}"
            WORKING_DIRECTORY "${_staged}"
            RESULT_VARIABLE _result
            ERROR_VARIABLE _error)
        if(NOT _result EQUAL 0)
            message(FATAL_ERROR "Cannot prepare ${name}: ${_error}")
        endif()
        file(WRITE "${_stamp}" "${_fingerprint}")
    endif()
    set(${output}
        "${_staged}"
        PARENT_SCOPE)
endfunction()

file(
    GLOB _efkbgfx_shader_sources CONFIGURE_DEPENDS
    RELATIVE "${_efkbgfx_source}"
    "${_efkbgfx_source}/shaders/*.sc" "${_efkbgfx_source}/shaders/*.sh")
_engine_stage_effects_source(
    efkbgfx
    "${_efkbgfx_source}"
    "${_effects_root}/patches/efkbgfx/effekseer-1808.patch"
    _efkbgfx_staged
    renderer/bgfxrenderer.cpp
    renderer/bgfxrenderer.h
    ${_efkbgfx_shader_sources})
set_property(GLOBAL PROPERTY ENGINE_EFKBGFX_SHADER_SOURCE "${_efkbgfx_staged}/shaders")
set(_effect_dependency_source Dev/Cpp/Effekseer/Effekseer/IO/Effekseer.EfkEfcFactory.cpp)
_engine_stage_effects_source(
    effekseer "${_effekseer_source}"
    "${_effects_root}/patches/effekseer/strict-effect-dependencies.patch" _effekseer_staged
    "${_effect_dependency_source}")
set_source_files_properties("${_effekseer_source}/${_effect_dependency_source}" TARGET_DIRECTORY
                            Effekseer PROPERTIES HEADER_FILE_ONLY TRUE)
target_sources(Effekseer PRIVATE "${_effekseer_staged}/${_effect_dependency_source}")
target_include_directories(Effekseer PRIVATE "${_effekseer_source}/Dev/Cpp/Effekseer/Effekseer/IO")

set(_effect_allocator_source Dev/Cpp/Effekseer/Effekseer/Utils/Effekseer.CustomAllocator.cpp)
_engine_stage_effects_source(
    effekseer-allocation "${_effekseer_source}"
    "${_effects_root}/patches/effekseer/portable-allocation.patch" _effekseer_allocator
    "${_effect_allocator_source}")
set_source_files_properties("${_effekseer_source}/${_effect_allocator_source}" TARGET_DIRECTORY
                            Effekseer PROPERTIES HEADER_FILE_ONLY TRUE)
target_sources(Effekseer PRIVATE "${_effekseer_allocator}/${_effect_allocator_source}")
target_include_directories(Effekseer PRIVATE "${_effekseer_source}/Dev/Cpp/Effekseer/Effekseer/Utils")

add_library(engine_effects_backend STATIC "${_efkbgfx_staged}/renderer/bgfxrenderer.cpp")
add_library(engine::effects_backend ALIAS engine_effects_backend)
target_include_directories(engine_effects_backend SYSTEM PUBLIC "${_efkbgfx_staged}/renderer")
target_link_libraries(engine_effects_backend PUBLIC Effekseer EffekseerRendererCommon bgfx::bgfx
                                                    bgfx::bx)
target_compile_features(engine_effects_backend PUBLIC cxx_std_20)
set_target_properties(engine_effects_backend PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(engine_effects_backend PRIVATE -Wno-inconsistent-missing-override)
endif()
engine_apply_compiler_cache(engine_effects_backend)

include("${PROJECT_SOURCE_DIR}/cmake/dependencies/effects_shaders.cmake")
