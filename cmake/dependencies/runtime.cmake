# Windows、macOS、Linux 和 Android 游戏运行时依赖。
find_package(SDL3 CONFIG REQUIRED GLOBAL)
find_package(bgfx CONFIG REQUIRED GLOBAL)
find_package(flecs CONFIG REQUIRED GLOBAL)
find_package(Jolt CONFIG REQUIRED GLOBAL)
find_package(recastnavigation CONFIG REQUIRED GLOBAL)
find_package(RmlUi CONFIG REQUIRED GLOBAL)
find_package(flatbuffers CONFIG REQUIRED GLOBAL)
find_package(basisu CONFIG REQUIRED GLOBAL)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ozz-animation/CMakeLists.txt")
    message(FATAL_ERROR "ozz-animation is missing; run git submodule update --init")
endif()

find_package(Git REQUIRED)
set(_ozz_source "${CMAKE_CURRENT_SOURCE_DIR}/ozz-animation")
set(_ozz_staged "${CMAKE_CURRENT_BINARY_DIR}/source/ozz-animation")
set(_ozz_overrides "${CMAKE_CURRENT_SOURCE_DIR}/overrides/ozz-animation")
set(_ozz_prepare_script "${PROJECT_SOURCE_DIR}/cmake/dependencies/prepare_dependency.cmake")
set(_ozz_stamp "${_ozz_staged}/.engine-stage-ready")
execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${_ozz_source}" rev-parse --git-path HEAD
    OUTPUT_VARIABLE _ozz_git_head
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _ozz_git_head_result)
if(NOT _ozz_git_head_result EQUAL 0)
    message(FATAL_ERROR "Cannot locate ozz-animation Git HEAD")
endif()
get_filename_component(_ozz_git_head "${_ozz_git_head}" ABSOLUTE BASE_DIR "${_ozz_source}")

# HEAD 可能只是符号引用；分支提交更新时必须同时观察 loose ref 或 packed-refs。
execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${_ozz_source}" symbolic-ref -q HEAD
    OUTPUT_VARIABLE _ozz_git_ref
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _ozz_git_ref_result)
if(NOT _ozz_git_ref_result EQUAL 0 AND NOT _ozz_git_ref_result EQUAL 1)
    message(FATAL_ERROR "Cannot read ozz-animation Git reference")
endif()
set(_ozz_git_paths "${_ozz_git_head}")
set(_ozz_git_names packed-refs)
if(_ozz_git_ref_result EQUAL 0)
    list(APPEND _ozz_git_names "${_ozz_git_ref}")
endif()
foreach(_git_name IN LISTS _ozz_git_names)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${_ozz_source}" rev-parse --git-path "${_git_name}"
        OUTPUT_VARIABLE _git_path
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _git_path_result)
    if(NOT _git_path_result EQUAL 0)
        message(FATAL_ERROR "Cannot locate ozz-animation Git metadata: ${_git_name}")
    endif()
    get_filename_component(_git_path "${_git_path}" ABSOLUTE BASE_DIR "${_ozz_source}")
    list(APPEND _ozz_git_paths "${_git_path}")
endforeach()
# 精确路径 glob 同时检测 loose ref / packed-refs 的创建和删除。
file(
    GLOB _ozz_git_files CONFIGURE_DEPENDS
    LIST_DIRECTORIES FALSE
    ${_ozz_git_paths})

file(
    GLOB_RECURSE _ozz_override_files CONFIGURE_DEPENDS
    LIST_DIRECTORIES FALSE
    "${_ozz_overrides}/*")
set_property(
    DIRECTORY
    APPEND
    PROPERTY CMAKE_CONFIGURE_DEPENDS "${_ozz_prepare_script}" ${_ozz_git_files}
             ${_ozz_override_files})

# 只使用 HEAD 的已提交源码；本地修复继续通过 overrides 提供。
execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${_ozz_source}" ls-tree -r --name-only HEAD
    OUTPUT_VARIABLE _ozz_source_manifest
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _ozz_source_manifest_result)
if(NOT _ozz_source_manifest_result EQUAL 0)
    message(FATAL_ERROR "Cannot list ozz-animation committed sources")
endif()
string(REPLACE "\n" ";" _ozz_source_files "${_ozz_source_manifest}")
set(_ozz_staged_files)
foreach(_source_file IN LISTS _ozz_source_files)
    list(APPEND _ozz_staged_files "${_ozz_staged}/${_source_file}")
endforeach()
foreach(_override_file IN LISTS _ozz_override_files)
    file(RELATIVE_PATH _relative_path "${_ozz_overrides}" "${_override_file}")
    list(APPEND _ozz_staged_files "${_ozz_staged}/${_relative_path}")
endforeach()
list(REMOVE_DUPLICATES _ozz_staged_files)

set(_ozz_prepare_arguments
    "-DENGINE_DEP_NAME=ozz-animation"
    "-DENGINE_DEP_SOURCE=${_ozz_source}"
    "-DENGINE_DEP_STAGED=${_ozz_staged}"
    "-DENGINE_DEP_OVERRIDES=${_ozz_overrides}"
    "-DENGINE_DEP_BUILD_ROOT=${CMAKE_BINARY_DIR}"
    "-DENGINE_DEP_STAMP=${_ozz_stamp}"
    "-DENGINE_DEP_GIT_EXECUTABLE=${GIT_EXECUTABLE}")
execute_process(COMMAND "${CMAKE_COMMAND}" ${_ozz_prepare_arguments} "-DENGINE_DEP_MODE=CONFIGURE"
                        -P "${_ozz_prepare_script}" RESULT_VARIABLE _ozz_prepare_result)
if(NOT _ozz_prepare_result EQUAL 0)
    message(FATAL_ERROR "Failed to prepare ozz-animation")
endif()
# Makefiles 不会为缺失的 BYPRODUCT 自动运行规则；目录清单变化先触发重新配置恢复。
file(
    GLOB_RECURSE _ozz_present_staged_files CONFIGURE_DEPENDS
    LIST_DIRECTORIES FALSE
    "${_ozz_staged}/*")

set(ozz_build_tools
    OFF
    CACHE BOOL "Build ozz tools" FORCE)
set(ozz_build_samples
    OFF
    CACHE BOOL "Build ozz samples" FORCE)
set(ozz_build_howtos
    OFF
    CACHE BOOL "Build ozz howtos" FORCE)
set(ozz_build_tests
    OFF
    CACHE BOOL "Build ozz tests" FORCE)
if(MSVC)
    # 匹配本项目与 x64-windows vcpkg 包的 /MDd、/MD，避免链接时混用 CRT。
    set(ozz_build_msvc_rt_dll
        ON
        CACHE BOOL "Use the project's dynamic MSVC runtime" FORCE)
endif()
add_subdirectory("${_ozz_staged}" "${CMAKE_CURRENT_BINARY_DIR}/ozz-animation" EXCLUDE_FROM_ALL)
# 上游使用目录级 include；显式向自有调用方传递 system include，保留上游警告策略。
foreach(_ozz_target ozz_animation ozz_animation_offline ozz_geometry ozz_base)
    target_include_directories(${_ozz_target} SYSTEM INTERFACE "${_ozz_staged}/include")
endforeach()

add_custom_command(
    OUTPUT "${_ozz_stamp}"
    BYPRODUCTS ${_ozz_staged_files} "${_ozz_staged}/.engine-stage-identity"
               "${_ozz_staged}/.engine-stage-revision" "${_ozz_staged}/.engine-stage-overrides"
    COMMAND "${CMAKE_COMMAND}" ${_ozz_prepare_arguments} "-DENGINE_DEP_MODE=BUILD" -P
            "${_ozz_prepare_script}"
    DEPENDS "${_ozz_prepare_script}" ${_ozz_git_files} ${_ozz_override_files}
    COMMENT "Preparing changed ozz-animation inputs"
    VERBATIM)
add_custom_target(engine_prepare_ozz_animation ALL DEPENDS "${_ozz_stamp}")

# 所有 ozz 目标（含源码融合目标）只依赖有变化才执行的准备规则。
function(_engine_ozz_add_prepare_dependency _directory)
    get_property(
        _targets
        DIRECTORY "${_directory}"
        PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_target IN LISTS _targets)
        add_dependencies("${_target}" engine_prepare_ozz_animation)
        get_target_property(_target_type "${_target}" TYPE)
        if(_target_type MATCHES
           "^(STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY|EXECUTABLE)$")
            engine_apply_compiler_cache("${_target}")
        endif()
    endforeach()
    get_property(
        _subdirectories
        DIRECTORY "${_directory}"
        PROPERTY SUBDIRECTORIES)
    foreach(_subdirectory IN LISTS _subdirectories)
        _engine_ozz_add_prepare_dependency("${_subdirectory}")
    endforeach()
endfunction()
_engine_ozz_add_prepare_dependency("${_ozz_staged}")

# V8 使用独立 GN 构建，由 v8.cmake 中的 ENGINE_ENABLE_V8 显式启用。
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/v8/BUILD.gn")
    message(FATAL_ERROR "V8 is missing; run git submodule update --init")
endif()
