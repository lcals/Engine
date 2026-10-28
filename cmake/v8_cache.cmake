# V8 专用成功缓存。只读取固定检出、补丁、小型版本文件和工具元数据，
# 不递归散列源码树，不运行 GN/Ninja，也不把“归档存在”当成构建成功证明。

function(_engine_v8_cache_keys _prepare_output _build_output _ready_output)
    set(${_ready_output} FALSE PARENT_SCOPE)
    set(_identity "${_revision}\n${_depot_revision}\n${_build_revision}\n${_partition_revision}\n")
    string(APPEND _identity "${CMAKE_HOST_SYSTEM_NAME}\n${ENGINE_V8_TARGET_CPU}\n")

    set(_roots "${_source}" "${_depot}" "${_source}/build"
        "${_source}/third_party/partition_alloc")
    set(_revisions "${_revision}" "${_depot_revision}" "${_build_revision}"
        "${_partition_revision}")
    foreach(_root _expected IN ZIP_LISTS _roots _revisions)
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${_root}" rev-parse HEAD
            RESULT_VARIABLE _status OUTPUT_VARIABLE _actual
            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(NOT _status EQUAL 0 OR NOT _actual STREQUAL _expected)
            return()
        endif()
        # 已跟踪的本地修改必须使缓存失效；不扫描巨大的未跟踪构建目录。
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${_root}"
            diff --no-ext-diff --binary HEAD --
            RESULT_VARIABLE _status OUTPUT_VARIABLE _diff ERROR_QUIET)
        if(NOT _status EQUAL 0)
            return()
        endif()
        string(SHA256 _diff_hash "${_diff}")
        string(APPEND _identity "${_actual}:${_diff_hash}\n")
    endforeach()
    foreach(_patch_root _patch IN ZIP_LISTS _patch_roots _patch_files)
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${_patch_root}"
            apply --reverse --check "${_patch}"
            RESULT_VARIABLE _status OUTPUT_QUIET ERROR_QUIET)
        if(NOT _status EQUAL 0)
            return()
        endif()
    endforeach()
    if(NOT EXISTS "${_workspace}/.gclient_entries")
        return()
    endif()
    # gclient 的小型清单给出其余 Git 依赖的固定提交；只读 HEAD，不扫描源码。
    file(STRINGS "${_workspace}/.gclient_entries" _git_entries REGEX "@[0-9a-f]+'")
    foreach(_entry IN LISTS _git_entries)
        if(_entry MATCHES "'([^']+)': '[^']+@([0-9a-f]+)'")
            set(_dependency "${_workspace}/${CMAKE_MATCH_1}")
            set(_expected "${CMAKE_MATCH_2}")
            execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${_dependency}" rev-parse HEAD
                RESULT_VARIABLE _status OUTPUT_VARIABLE _actual
                OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
            if(NOT _status EQUAL 0 OR NOT _actual STREQUAL _expected)
                return()
            endif()
            string(APPEND _identity "${_dependency}:${_actual}\n")
        endif()
    endforeach()

    set(_files
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/prepare_v8.cmake"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/v8_cache.cmake"
        "${_engine_root}/third_party/v8.cmake"
        "${ENGINE_V8_BUILD_ROOT}/.gitconfig"
        "${_source}/DEPS" "${_workspace}/.gclient" "${_workspace}/.gclient_entries"
        "${_source}/third_party/llvm-build/Release+Asserts/cr_build_revision"
        "${_source}/third_party/rust-toolchain/VERSION"
        ${_patch_files})
    if(DEFINED ENGINE_V8_HOST_TOOLCHAIN_FILE)
        list(APPEND _files "${ENGINE_V8_HOST_TOOLCHAIN_FILE}")
    endif()
    set(_tools "${_gn}" "${_ninja}")
    if(CMAKE_HOST_WIN32)
        list(APPEND _tools
            "${_source}/third_party/llvm-build/Release+Asserts/bin/clang-cl.exe"
            "${_source}/third_party/llvm-build/Release+Asserts/bin/lld-link.exe"
            "${_source}/third_party/rust-toolchain/bin/rustc.exe")
        # 按系统注册位置及上游支持的 VS 选择变量检测，不能假定安装在 C:。
        cmake_host_system_information(RESULT _program_files_x86 QUERY WINDOWS_REGISTRY
            "HKLM/SOFTWARE/Microsoft/Windows/CurrentVersion" VALUE "ProgramFilesDir (x86)"
            ERROR_VARIABLE _registry_error)
        cmake_host_system_information(RESULT _sdk_root QUERY WINDOWS_REGISTRY
            "HKLM/SOFTWARE/Microsoft/Windows Kits/Installed Roots" VALUE KitsRoot10
            ERROR_VARIABLE _registry_error)
        set(_vs_roots "$ENV{GYP_MSVS_OVERRIDE_PATH}" "$ENV{vs2022_install}"
            "$ENV{vs2019_install}" "$ENV{vs2017_install}")
        file(GLOB _default_vs LIST_DIRECTORIES TRUE
            "$ENV{ProgramFiles}/Microsoft Visual Studio/*/*"
            "${_program_files_x86}/Microsoft Visual Studio/*/*")
        list(APPEND _vs_roots ${_default_vs})
        set(_windows_versions)
        foreach(_vs_root IN LISTS _vs_roots)
            if(NOT _vs_root STREQUAL "")
                set(_version "${_vs_root}/VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt")
                if(EXISTS "${_version}")
                    list(APPEND _windows_versions "${_version}")
                endif()
            endif()
        endforeach()
        if(NOT _sdk_root STREQUAL "")
            file(GLOB _sdk_versions "${_sdk_root}/Include/*/um/Windows.h"
                "${_sdk_root}/Include/*/ucrt/corecrt.h")
            list(APPEND _windows_versions ${_sdk_versions})
        endif()
        list(REMOVE_DUPLICATES _windows_versions)
        list(SORT _windows_versions)
        list(APPEND _files ${_windows_versions})
        string(APPEND _identity "$ENV{GYP_MSVS_OVERRIDE_PATH}\n$ENV{GYP_MSVS_VERSION}\n"
            "$ENV{vs2022_install}\n$ENV{vs2019_install}\n$ENV{vs2017_install}\n")
    else()
        list(APPEND _tools
            "${_source}/third_party/llvm-build/Release+Asserts/bin/clang"
            "${_source}/third_party/llvm-build/Release+Asserts/bin/llvm-ar"
            "${_source}/third_party/rust-toolchain/bin/rustc")
    endif()
    if(CMAKE_HOST_APPLE)
        foreach(_query IN ITEMS --show-sdk-path --show-sdk-version)
            execute_process(COMMAND xcrun --sdk macosx "${_query}"
                RESULT_VARIABLE _status OUTPUT_VARIABLE _sdk_value
                OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
            if(NOT _status EQUAL 0)
                return()
            endif()
            string(APPEND _identity "${_sdk_value}\n")
            if(_query STREQUAL "--show-sdk-path")
                list(APPEND _files "${_sdk_value}/SDKSettings.json")
            endif()
        endforeach()
        foreach(_tool IN ITEMS ld lipo)
            execute_process(COMMAND xcrun --find "${_tool}"
                RESULT_VARIABLE _status OUTPUT_VARIABLE _tool_path
                OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
            if(NOT _status EQUAL 0)
                return()
            endif()
            list(APPEND _tools "${_tool_path}")
        endforeach()
        string(APPEND _identity "$ENV{DEVELOPER_DIR}\n$ENV{SDKROOT}\n")
    elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
        # 当前固定 Chromium build 的 native x64 默认 sysroot；缺失时必须跑 hooks。
        list(APPEND _files "${_source}/build/linux/debian_bullseye_amd64-sysroot/.stamp"
            "${_source}/build/linux/sysroot_scripts/sysroots.json")
    endif()
    if(DEFINED ENGINE_V8_HOST_COMPILER AND NOT ENGINE_V8_HOST_COMPILER STREQUAL "")
        list(APPEND _tools "${ENGINE_V8_HOST_COMPILER}")
    endif()
    foreach(_file IN LISTS _files)
        if(NOT EXISTS "${_file}")
            return()
        endif()
        file(SHA256 "${_file}" _hash)
        # Host manifest 的内容可跨 preset 共用，不能把各 build 目录路径计入身份。
        if(_file STREQUAL ENGINE_V8_HOST_TOOLCHAIN_FILE)
            string(APPEND _identity "host-toolchain:${_hash}\n")
        else()
            string(APPEND _identity "${_file}:${_hash}\n")
        endif()
    endforeach()
    foreach(_tool IN LISTS _tools)
        if(NOT EXISTS "${_tool}")
            return()
        endif()
        file(SIZE "${_tool}" _size)
        file(TIMESTAMP "${_tool}" _mtime "%Y-%m-%dT%H:%M:%S.%fZ" UTC)
        string(APPEND _identity "${_tool}:${_size}:${_mtime}\n")
    endforeach()
    string(SHA256 _prepare_key "${_identity}")
    file(SHA256 "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/package_v8.cmake" _package_hash)
    string(SHA256 _build_key "${_prepare_key}\n${_args}\n${_package_hash}\n")
    set(${_prepare_output} "${_prepare_key}" PARENT_SCOPE)
    set(${_build_output} "${_build_key}" PARENT_SCOPE)
    set(${_ready_output} TRUE PARENT_SCOPE)
endfunction()

function(_engine_v8_stamp_matches _file _key _output)
    set(_matches FALSE)
    if(EXISTS "${_file}")
        file(READ "${_file}" _saved)
        if(_saved STREQUAL _key)
            set(_matches TRUE)
        endif()
    endif()
    set(${_output} ${_matches} PARENT_SCOPE)
endfunction()

function(_engine_v8_artifacts _output)
    set(${_output} FALSE PARENT_SCOPE)
    foreach(_artifact IN ITEMS "${_out}/obj/${_library}" "${_out}/icudtl.dat"
            "${_out}/gen/include/v8-gn.h" "${_sample}" "${_out}/args.gn"
            "${_out}/build.ninja")
        if(NOT EXISTS "${_artifact}")
            return()
        endif()
        file(SIZE "${_artifact}" _size)
        if(_size EQUAL 0)
            return()
        endif()
    endforeach()
    file(READ "${_out}/args.gn" _saved_args)
    if(_saved_args STREQUAL _args)
        set(${_output} TRUE PARENT_SCOPE)
    endif()
endfunction()

function(_engine_v8_headers_current _output)
    set(${_output} FALSE PARENT_SCOPE)
    set(_generated "${ENGINE_V8_BUILD_ROOT}/include/${_config_lower}/v8-gn.h")
    if(NOT EXISTS "${_generated}" OR NOT EXISTS "${_out}/gen/include/v8-gn.h")
        return()
    endif()
    file(SIZE "${_generated}" _size)
    if(_size EQUAL 0)
        return()
    endif()
    file(SHA256 "${_generated}" _copied_hash)
    file(SHA256 "${_out}/gen/include/v8-gn.h" _source_hash)
    if(NOT _copied_hash STREQUAL _source_hash)
        return()
    endif()
    # 这里只核对约数百个公开头文件，不扫描整个 V8 或构建产物树。
    file(GLOB_RECURSE _headers LIST_DIRECTORIES FALSE RELATIVE "${_source}/include"
        "${_source}/include/*")
    if(NOT _headers)
        return()
    endif()
    foreach(_header IN LISTS _headers)
        set(_copy "${ENGINE_V8_BUILD_ROOT}/include/v8/${_header}")
        if(NOT EXISTS "${_copy}")
            return()
        endif()
        file(SHA256 "${_source}/include/${_header}" _source_hash)
        file(SHA256 "${_copy}" _copied_hash)
        if(NOT _copied_hash STREQUAL _source_hash)
            return()
        endif()
    endforeach()
    set(${_output} TRUE PARENT_SCOPE)
endfunction()

function(_engine_v8_write_stamp _file _key)
    file(WRITE "${_file}.tmp" "${_key}")
    file(RENAME "${_file}.tmp" "${_file}")
endfunction()
