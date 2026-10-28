# 在构建目录准备固定版本的 Git 子项目副本，并覆盖本地文件。
# add_subdirectory() 前执行一次，此后每次构建前再次执行。

foreach(_required IN ITEMS ENGINE_DEP_NAME ENGINE_DEP_SOURCE ENGINE_DEP_STAGED
                           ENGINE_DEP_OVERRIDES ENGINE_DEP_BUILD_ROOT ENGINE_DEP_MODE
                           ENGINE_DEP_GIT_EXECUTABLE)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "Missing ${_required} for dependency preparation")
    endif()
endforeach()

if(NOT ENGINE_DEP_MODE STREQUAL "CONFIGURE" AND NOT ENGINE_DEP_MODE STREQUAL "BUILD")
    message(FATAL_ERROR "ENGINE_DEP_MODE must be CONFIGURE or BUILD")
endif()

file(REAL_PATH "${ENGINE_DEP_BUILD_ROOT}" _build_root)
cmake_path(IS_PREFIX _build_root "${ENGINE_DEP_STAGED}" NORMALIZE _stage_is_in_build)
if(NOT _stage_is_in_build OR "${ENGINE_DEP_STAGED}" STREQUAL "${_build_root}")
    message(FATAL_ERROR "Staged dependency path must be inside the build tree: ${ENGINE_DEP_STAGED}")
endif()
if(EXISTS "${ENGINE_DEP_STAGED}")
    file(REAL_PATH "${ENGINE_DEP_STAGED}" _staged_real)
    cmake_path(IS_PREFIX _build_root "${_staged_real}" NORMALIZE _resolved_stage_is_in_build)
    if(NOT _resolved_stage_is_in_build OR _staged_real STREQUAL _build_root)
        message(FATAL_ERROR "Resolved staged dependency path escapes the build tree: ${ENGINE_DEP_STAGED}")
    endif()
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
    message(FATAL_ERROR "${ENGINE_DEP_NAME} is not an initialized Git subproject: ${_git_root_error}")
endif()
file(REAL_PATH "${ENGINE_DEP_SOURCE}" _source_root)
file(REAL_PATH "${_git_root}" _git_root_real)
if(WIN32)
    string(TOLOWER "${_source_root}" _source_root)
    string(TOLOWER "${_git_root_real}" _git_root_real)
endif()
if(NOT _source_root STREQUAL _git_root_real)
    message(FATAL_ERROR "${ENGINE_DEP_NAME} must be its own Git subproject, not the parent repository")
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

file(GLOB_RECURSE _override_files LIST_DIRECTORIES FALSE
     RELATIVE "${ENGINE_DEP_OVERRIDES}" "${ENGINE_DEP_OVERRIDES}/*")
list(SORT _override_files)
string(JOIN "\n" _override_manifest ${_override_files})
string(SHA256 _stage_identity "${_git_revision}\n${_override_manifest}")
set(_identity_file "${ENGINE_DEP_STAGED}/.engine-stage-identity")

set(_existing_identity "")
if(EXISTS "${_identity_file}")
    file(READ "${_identity_file}" _existing_identity)
endif()

if(NOT _existing_identity STREQUAL _stage_identity OR
   NOT EXISTS "${ENGINE_DEP_STAGED}/CMakeLists.txt")
    if(ENGINE_DEP_MODE STREQUAL "BUILD")
        message(FATAL_ERROR
            "${ENGINE_DEP_NAME} upstream revision or override file list changed; rerun CMake configure before building")
    endif()

    # git archive 只读取已提交版本；副本不包含本地改动和 .git 数据。
    file(REMOVE_RECURSE "${ENGINE_DEP_STAGED}")
    file(MAKE_DIRECTORY "${ENGINE_DEP_STAGED}")
    set(_archive "${ENGINE_DEP_STAGED}.tar")
    execute_process(
        COMMAND "${ENGINE_DEP_GIT_EXECUTABLE}" -C "${ENGINE_DEP_SOURCE}"
                archive --format=tar "--output=${_archive}" HEAD
        RESULT_VARIABLE _archive_result
        ERROR_VARIABLE _archive_error)
    if(NOT _archive_result EQUAL 0)
        message(FATAL_ERROR "Cannot stage ${ENGINE_DEP_NAME}: ${_archive_error}")
    endif()
    file(ARCHIVE_EXTRACT INPUT "${_archive}" DESTINATION "${ENGINE_DEP_STAGED}")
    file(REMOVE "${_archive}")

    # archive 保留旧时间戳；恢复上游文件时刷新时间，确保增量编译生效。
    file(GLOB_RECURSE _staged_files LIST_DIRECTORIES FALSE "${ENGINE_DEP_STAGED}/*")
    foreach(_staged_file IN LISTS _staged_files)
        if(NOT IS_SYMLINK "${_staged_file}")
            file(TOUCH_NOCREATE "${_staged_file}")
        endif()
    endforeach()
    file(WRITE "${_identity_file}" "${_stage_identity}")
endif()

foreach(_relative_path IN LISTS _override_files)
    set(_source_file "${ENGINE_DEP_OVERRIDES}/${_relative_path}")
    set(_staged_file "${ENGINE_DEP_STAGED}/${_relative_path}")
    get_filename_component(_staged_dir "${_staged_file}" DIRECTORY)
    file(MAKE_DIRECTORY "${_staged_dir}")
    file(COPY_FILE "${_source_file}" "${_staged_file}" ONLY_IF_DIFFERENT)
endforeach()
