cmake_minimum_required(VERSION 3.25)

# 准备固定 Git 提交的副本。无变化的构建不执行本脚本；覆盖修改只替换对应文件。
foreach(
    _required IN
    ITEMS ENGINE_DEP_NAME
          ENGINE_DEP_SOURCE
          ENGINE_DEP_STAGED
          ENGINE_DEP_OVERRIDES
          ENGINE_DEP_BUILD_ROOT
          ENGINE_DEP_MODE
          ENGINE_DEP_STAMP
          ENGINE_DEP_GIT_EXECUTABLE)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "Missing ${_required} for dependency preparation")
    endif()
endforeach()

if(NOT ENGINE_DEP_MODE STREQUAL "CONFIGURE" AND NOT ENGINE_DEP_MODE STREQUAL "BUILD")
    message(FATAL_ERROR "ENGINE_DEP_MODE must be CONFIGURE or BUILD")
endif()

file(REAL_PATH "${ENGINE_DEP_BUILD_ROOT}" _build_root)
cmake_path(SET _build_path NORMALIZE "${ENGINE_DEP_BUILD_ROOT}")
cmake_path(IS_PREFIX _build_path "${ENGINE_DEP_STAGED}" NORMALIZE _stage_is_in_build)
if(NOT _stage_is_in_build OR "${ENGINE_DEP_STAGED}" STREQUAL "${_build_path}")
    message(
        FATAL_ERROR "Staged dependency path must be inside the build tree: ${ENGINE_DEP_STAGED}")
endif()
if(EXISTS "${ENGINE_DEP_STAGED}")
    file(REAL_PATH "${ENGINE_DEP_STAGED}" _staged_real)
    cmake_path(IS_PREFIX _build_root "${_staged_real}" NORMALIZE _resolved_stage_is_in_build)
    if(NOT _resolved_stage_is_in_build OR _staged_real STREQUAL _build_root)
        message(
            FATAL_ERROR
                "Resolved staged dependency path escapes the build tree: ${ENGINE_DEP_STAGED}")
    endif()
endif()
cmake_path(IS_PREFIX _build_path "${ENGINE_DEP_STAMP}" NORMALIZE _stamp_is_in_build)
if(NOT _stamp_is_in_build OR "${ENGINE_DEP_STAMP}" STREQUAL "${_build_path}")
    message(FATAL_ERROR "Dependency stamp must be inside the build tree: ${ENGINE_DEP_STAMP}")
endif()

if(NOT EXISTS "${ENGINE_DEP_SOURCE}/CMakeLists.txt")
    message(FATAL_ERROR "${ENGINE_DEP_NAME} source is missing: ${ENGINE_DEP_SOURCE}")
endif()
execute_process(
    COMMAND "${ENGINE_DEP_GIT_EXECUTABLE}" -C "${ENGINE_DEP_SOURCE}" rev-parse --show-toplevel
    RESULT_VARIABLE _git_root_result
    OUTPUT_VARIABLE _git_root
    ERROR_VARIABLE _git_root_error
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT _git_root_result EQUAL 0)
    message(
        FATAL_ERROR "${ENGINE_DEP_NAME} is not an initialized Git subproject: ${_git_root_error}")
endif()
file(REAL_PATH "${ENGINE_DEP_SOURCE}" _source_root)
file(REAL_PATH "${_git_root}" _git_root_real)
if(WIN32)
    string(TOLOWER "${_source_root}" _source_root)
    string(TOLOWER "${_git_root_real}" _git_root_real)
endif()
if(NOT _source_root STREQUAL _git_root_real)
    message(
        FATAL_ERROR "${ENGINE_DEP_NAME} must be its own Git subproject, not the parent repository")
endif()

execute_process(
    COMMAND "${ENGINE_DEP_GIT_EXECUTABLE}" -C "${ENGINE_DEP_SOURCE}" rev-parse --verify HEAD
    RESULT_VARIABLE _git_revision_result
    OUTPUT_VARIABLE _git_revision
    ERROR_VARIABLE _git_revision_error
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT _git_revision_result EQUAL 0)
    message(FATAL_ERROR "Cannot read ${ENGINE_DEP_NAME} revision: ${_git_revision_error}")
endif()
execute_process(
    COMMAND "${ENGINE_DEP_GIT_EXECUTABLE}" -C "${ENGINE_DEP_SOURCE}" ls-tree -r --name-only HEAD
    RESULT_VARIABLE _source_manifest_result
    OUTPUT_VARIABLE _source_manifest
    ERROR_VARIABLE _source_manifest_error
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT _source_manifest_result EQUAL 0)
    message(FATAL_ERROR "Cannot list ${ENGINE_DEP_NAME} sources: ${_source_manifest_error}")
endif()
string(REPLACE "\n" ";" _source_files "${_source_manifest}")

file(
    GLOB_RECURSE _override_files
    LIST_DIRECTORIES FALSE
    RELATIVE "${ENGINE_DEP_OVERRIDES}"
    "${ENGINE_DEP_OVERRIDES}/*")
list(SORT _override_files)
string(JOIN "\n" _override_manifest ${_override_files})
set(_revision_file "${ENGINE_DEP_STAGED}/.engine-stage-revision")
set(_overrides_file "${ENGINE_DEP_STAGED}/.engine-stage-overrides")
set(_existing_revision "")
set(_previous_overrides "")
if(EXISTS "${_revision_file}")
    file(READ "${_revision_file}" _existing_revision)
endif()
if(EXISTS "${_overrides_file}")
    file(READ "${_overrides_file}" _previous_overrides)
endif()

# 副本中的 CMake 文件也是生成器输入；先同步，再由 CMake 自动重新配置。
set(_restore_upstream FALSE)
if(NOT _existing_revision STREQUAL _git_revision)
    set(_restore_upstream TRUE)
endif()
string(REPLACE "\n" ";" _previous_override_files "${_previous_overrides}")
foreach(_relative_path IN LISTS _previous_override_files)
    if(NOT _relative_path IN_LIST _override_files)
        set(_restore_upstream TRUE)
    endif()
endforeach()
foreach(_relative_path IN LISTS _source_files)
    if(NOT EXISTS "${ENGINE_DEP_STAGED}/${_relative_path}" AND NOT _relative_path IN_LIST
                                                               _override_files)
        set(_restore_upstream TRUE)
    endif()
endforeach()

set(_snapshot "${ENGINE_DEP_STAGED}.upstream")
if(_restore_upstream)
    # git archive 刻意忽略未提交的 checkout 修改；补丁只从 overrides 应用。
    file(REMOVE_RECURSE "${_snapshot}")
    file(MAKE_DIRECTORY "${_snapshot}")
    set(_archive "${ENGINE_DEP_STAGED}.tar")
    execute_process(
        COMMAND "${ENGINE_DEP_GIT_EXECUTABLE}" -C "${ENGINE_DEP_SOURCE}" archive --format=tar
                "--output=${_archive}" HEAD
        RESULT_VARIABLE _archive_result
        ERROR_VARIABLE _archive_error)
    if(NOT _archive_result EQUAL 0)
        message(FATAL_ERROR "Cannot stage ${ENGINE_DEP_NAME}: ${_archive_error}")
    endif()
    file(ARCHIVE_EXTRACT INPUT "${_archive}" DESTINATION "${_snapshot}")
    file(REMOVE "${_archive}")
endif()

set(_expected_files ${_source_files} ${_override_files})
list(REMOVE_DUPLICATES _expected_files)
file(MAKE_DIRECTORY "${ENGINE_DEP_STAGED}")
file(
    GLOB_RECURSE _staged_files
    LIST_DIRECTORIES FALSE
    RELATIVE "${ENGINE_DEP_STAGED}"
    "${ENGINE_DEP_STAGED}/*")
foreach(_relative_path IN LISTS _staged_files)
    if(NOT _relative_path IN_LIST _expected_files AND NOT _relative_path MATCHES
                                                      "^\\.engine-stage-")
        file(REMOVE "${ENGINE_DEP_STAGED}/${_relative_path}")
    endif()
endforeach()

foreach(_relative_path IN LISTS _expected_files)
    if(_relative_path IN_LIST _override_files)
        set(_source_file "${ENGINE_DEP_OVERRIDES}/${_relative_path}")
    elseif(_restore_upstream)
        set(_source_file "${_snapshot}/${_relative_path}")
    else()
        continue()
    endif()
    set(_staged_file "${ENGINE_DEP_STAGED}/${_relative_path}")
    get_filename_component(_staged_dir "${_staged_file}" DIRECTORY)
    file(MAKE_DIRECTORY "${_staged_dir}")
    if(IS_SYMLINK "${_source_file}")
        file(READ_SYMLINK "${_source_file}" _link_target)
        set(_existing_target "")
        if(IS_SYMLINK "${_staged_file}")
            file(READ_SYMLINK "${_staged_file}" _existing_target)
        endif()
        if(NOT _existing_target STREQUAL _link_target)
            file(REMOVE "${_staged_file}")
            file(CREATE_LINK "${_link_target}" "${_staged_file}" SYMBOLIC)
        endif()
    else()
        if(IS_SYMLINK "${_staged_file}")
            file(REMOVE "${_staged_file}")
        endif()
        # 保留所有未改变文件的时间戳，版本更新和移除覆盖也不会全量触摸源码。
        file(COPY_FILE "${_source_file}" "${_staged_file}" ONLY_IF_DIFFERENT)
    endif()
endforeach()
if(_restore_upstream)
    file(REMOVE_RECURSE "${_snapshot}")
endif()

file(SHA256 "${CMAKE_CURRENT_LIST_FILE}" _prepare_hash)
set(_identity_input "${_git_revision}\n${_prepare_hash}")
foreach(_relative_path IN LISTS _override_files)
    file(SHA256 "${ENGINE_DEP_OVERRIDES}/${_relative_path}" _override_hash)
    string(APPEND _identity_input "\n${_relative_path}:${_override_hash}")
endforeach()
string(SHA256 _stage_identity "${_identity_input}")
file(CONFIGURE OUTPUT "${ENGINE_DEP_STAGED}/.engine-stage-identity" CONTENT "${_stage_identity}"
     @ONLY)
file(CONFIGURE OUTPUT "${_revision_file}" CONTENT "${_git_revision}" @ONLY)
file(CONFIGURE OUTPUT "${_overrides_file}" CONTENT "${_override_manifest}" @ONLY)
file(TOUCH "${ENGINE_DEP_STAMP}")
