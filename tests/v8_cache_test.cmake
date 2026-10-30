cmake_minimum_required(VERSION 3.25)
if(NOT DEFINED ENGINE_OUTPUT_DIR OR ENGINE_OUTPUT_DIR STREQUAL "")
    message(FATAL_ERROR "Missing isolated ENGINE_OUTPUT_DIR")
endif()
find_package(Git REQUIRED)
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/dependencies/v8_cache.cmake")

# 关闭 Intl 后只允许移除旧生成物依赖的精确升级，保留其他手工配置保护。
set(_legacy_args "v8_enable_i18n_support = false\nis_debug = false\n")
set(_current_args
    "${_legacy_args}v8_depend_on_icu_data_file = false\nicu_copy_icudata_to_root_build_dir = false\n"
)
_engine_v8_arguments_compatible("${_legacy_args}" "${_current_args}" _compatible)
if(NOT _compatible)
    message(FATAL_ERROR "The known no-ICU GN argument migration must preserve the existing cache")
endif()
_engine_v8_arguments_compatible("${_current_args}" "${_current_args}" _compatible)
if(NOT _compatible)
    message(FATAL_ERROR "Identical GN arguments must remain accepted")
endif()
_engine_v8_arguments_compatible("${_legacy_args}use_lld = true\n" "${_current_args}" _compatible)
if(_compatible)
    message(FATAL_ERROR "Manual GN changes must not be overwritten by the ICU migration")
endif()
string(REPLACE "v8_enable_i18n_support = false" "v8_enable_i18n_support = true" _full_args
               "${_current_args}")
_engine_v8_arguments_compatible("${_legacy_args}" "${_full_args}" _compatible)
if(_compatible)
    message(FATAL_ERROR "ICU migration must not change the requested Intl configuration")
endif()

# 仅使用独立小型假产物；此测试不运行 V8、GN、Ninja 或归档器。
set(_engine_root "${ENGINE_OUTPUT_DIR}/fixture")
file(REMOVE_RECURSE "${_engine_root}")
set(ENGINE_V8_BUILD_ROOT "${_engine_root}/v8-build")
set(ENGINE_V8_TARGET_CPU x64)
set(_workspace "${ENGINE_V8_BUILD_ROOT}/source")
set(_source "${_workspace}/v8")
set(_depot "${ENGINE_V8_BUILD_ROOT}/depot_tools")
set(_config_lower debug)
set(_out "${_source}/out/engine.debug")
set(_args "is_debug = true\n")
set(_gn "${_source}/gn")
set(_ninja "${_source}/ninja")
set(_library fixture.a)
set(_sample "${_out}/fixture-sample")

function(write_fixture _path _content)
    get_filename_component(_directory "${_path}" DIRECTORY)
    file(MAKE_DIRECTORY "${_directory}")
    file(WRITE "${_path}" "${_content}")
endfunction()

set(_roots "${_source}" "${_depot}" "${_source}/build" "${_source}/third_party/partition_alloc")
set(_revision_variables _revision _depot_revision _build_revision _partition_revision)
foreach(_root _variable IN ZIP_LISTS _roots _revision_variables)
    file(MAKE_DIRECTORY "${_root}")
    execute_process(COMMAND "${GIT_EXECUTABLE}" init --quiet "${_root}" COMMAND_ERROR_IS_FATAL ANY)
    execute_process(
        COMMAND
            "${GIT_EXECUTABLE}" -C "${_root}" -c user.name=EngineTest -c
            user.email=engine-test@example.invalid -c commit.gpgsign=false commit --quiet
            --allow-empty -m fixture COMMAND_ERROR_IS_FATAL ANY)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${_root}" rev-parse HEAD
        OUTPUT_VARIABLE ${_variable}
        OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
endforeach()

set(_files
    "${_engine_root}/cmake/dependencies/v8.cmake"
    "${_source}/DEPS"
    "${_workspace}/.gclient"
    "${_workspace}/.gclient_entries"
    "${_source}/third_party/llvm-build/Release+Asserts/cr_build_revision"
    "${_source}/third_party/rust-toolchain/VERSION"
    "${_gn}"
    "${_ninja}")
if(CMAKE_HOST_WIN32)
    list(APPEND _files "${_source}/third_party/llvm-build/Release+Asserts/bin/clang-cl.exe"
         "${_source}/third_party/llvm-build/Release+Asserts/bin/lld-link.exe"
         "${_source}/third_party/rust-toolchain/bin/rustc.exe")
else()
    list(APPEND _files "${_source}/third_party/llvm-build/Release+Asserts/bin/clang"
         "${_source}/third_party/llvm-build/Release+Asserts/bin/llvm-ar"
         "${_source}/third_party/rust-toolchain/bin/rustc")
endif()
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND _files "${_source}/build/linux/debian_bullseye_amd64-sysroot/.stamp"
         "${_source}/build/linux/sysroot_scripts/sysroots.json")
endif()
foreach(_file IN LISTS _files)
    write_fixture("${_file}" "fixture-input\n")
endforeach()
write_fixture("${ENGINE_V8_BUILD_ROOT}/.gitconfig" "[core]\n    filemode = false\n")
write_fixture("${_workspace}/.gclient_entries"
              "entries = { 'v8/build': 'https://example.invalid/build@${_build_revision}' }\n")

_engine_v8_cache_keys(_prepare_key _original_key _ready)
if(NOT _ready)
    message(FATAL_ERROR "Complete fixture must have a valid cache identity")
endif()
set(_stamp "${ENGINE_V8_BUILD_ROOT}/success.identity")
_engine_v8_write_stamp("${_stamp}" "${_original_key}")
_engine_v8_stamp_matches("${_stamp}" "${_original_key}" _matches)
if(NOT _matches)
    message(FATAL_ERROR "Identical inputs must match a completed cache stamp")
endif()

# Feature and CRT variants reuse the prepared toolchain, but never its compiled ABI.
set(_original_prepare_key "${_prepare_key}")
set(_original_args "${_args}")
set(_variant_keys)
foreach(
    _argument IN
    ITEMS "v8_enable_webassembly = false"
          "v8_enable_i18n_support = false"
          "v8_enable_temporal_support = false"
          "v8_enable_system_instrumentation = false"
          "engine_use_static_crt = true"
          "v8_depend_on_icu_data_file = false"
          "icu_copy_icudata_to_root_build_dir = false")
    set(_args "${_original_args}${_argument}\n")
    _engine_v8_cache_keys(_variant_prepare_key _variant_build_key _ready)
    if(NOT _ready
       OR NOT _variant_prepare_key STREQUAL _original_prepare_key
       OR _variant_build_key STREQUAL _original_key
       OR _variant_build_key IN_LIST _variant_keys)
        message(
            FATAL_ERROR "Feature/CRT variants must have distinct build identities: ${_argument}")
    endif()
    list(APPEND _variant_keys "${_variant_build_key}")
endforeach()
set(_args "${_original_args}")

foreach(_input IN ITEMS "${_source}/DEPS" "${_engine_root}/cmake/dependencies/v8.cmake"
                        "${_source}/third_party/rust-toolchain/VERSION")
    write_fixture("${_input}" "changed-input\n")
    _engine_v8_cache_keys(_prepare_key _changed_key _ready)
    _engine_v8_stamp_matches("${_stamp}" "${_changed_key}" _matches)
    if(NOT _ready
       OR _matches
       OR _changed_key STREQUAL _original_key)
        message(FATAL_ERROR "Changed input must invalidate the cache: ${_input}")
    endif()
    write_fixture("${_input}" "fixture-input\n")
endforeach()

set(_saved_cpu "${ENGINE_V8_TARGET_CPU}")
set(ENGINE_V8_TARGET_CPU changed)
_engine_v8_cache_keys(_prepare_key _changed_key _ready)
if(NOT _ready OR _changed_key STREQUAL _original_key)
    message(FATAL_ERROR "Changed target CPU must invalidate the cache")
endif()
set(ENGINE_V8_TARGET_CPU "${_saved_cpu}")

file(REMOVE "${_gn}")
_engine_v8_cache_keys(_prepare_key _changed_key _ready)
if(_ready)
    message(FATAL_ERROR "Missing tool must reject the prepared workspace")
endif()
write_fixture("${_gn}" "fixture-input\n")

# 新增独立补丁根：补丁仍然正确应用时，其余已跟踪源码变化也要失效缓存。
set(_additional_root "${_source}/third_party/simdutf")
write_fixture("${_additional_root}/patched.cpp" "upstream\n")
write_fixture("${_additional_root}/other.cpp" "unchanged\n")
execute_process(COMMAND "${GIT_EXECUTABLE}" init --quiet "${_additional_root}"
                        COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${_additional_root}" add patched.cpp other.cpp
                        COMMAND_ERROR_IS_FATAL ANY)
execute_process(
    COMMAND
        "${GIT_EXECUTABLE}" -C "${_additional_root}" -c user.name=EngineTest -c
        user.email=engine-test@example.invalid -c commit.gpgsign=false commit --quiet -m fixture
        COMMAND_ERROR_IS_FATAL ANY)
execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${_additional_root}" rev-parse HEAD
    OUTPUT_VARIABLE _additional_revision
    OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
write_fixture("${_additional_root}/patched.cpp" "patched\n")
execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${_additional_root}" diff --no-ext-diff --binary HEAD --
            patched.cpp OUTPUT_VARIABLE _additional_patch COMMAND_ERROR_IS_FATAL ANY)
set(_patch_roots "${_additional_root}")
set(_patch_files "${_engine_root}/additional.patch")
write_fixture("${_patch_files}" "${_additional_patch}")
file(READ "${_workspace}/.gclient_entries" _original_entries)
write_fixture(
    "${_workspace}/.gclient_entries"
    "entries = {\n 'v8/build': 'https://example.invalid/build@${_build_revision}',\n 'v8/third_party/simdutf': 'https://example.invalid/simdutf@${_additional_revision}'\n}\n"
)
_engine_v8_cache_keys(_prepare_key _additional_key _ready)
if(NOT _ready)
    message(FATAL_ERROR "A verified additional patch root must have a valid cache identity")
endif()
write_fixture("${_additional_root}/other.cpp" "changed outside the known patch\n")
_engine_v8_cache_keys(_prepare_key _changed_key _ready)
if(NOT _ready OR _changed_key STREQUAL _additional_key)
    message(
        FATAL_ERROR "Tracked changes outside the known patch must invalidate an additional root")
endif()
execute_process(
    COMMAND
        "${GIT_EXECUTABLE}" -C "${_additional_root}" -c user.name=EngineTest -c
        user.email=engine-test@example.invalid -c commit.gpgsign=false commit --quiet --allow-empty
        -m changed-revision COMMAND_ERROR_IS_FATAL ANY)
_engine_v8_cache_keys(_prepare_key _changed_key _ready)
if(_ready)
    message(FATAL_ERROR "Additional patch root HEAD must still match the gclient revision")
endif()
unset(_patch_roots)
unset(_patch_files)
write_fixture("${_workspace}/.gclient_entries" "${_original_entries}")

# Android 目标使用独立身份，并核对宿主运行的 NDK 工具；不会执行交叉产物。
set(ENGINE_V8_TARGET_CPU arm64)
_engine_v8_cache_keys(_prepare_key _native_key _ready)
if(NOT _ready)
    message(FATAL_ERROR "Native fixture must remain ready before Android identity checks")
endif()
set(ENGINE_V8_TARGET_OS android)
_engine_v8_cache_keys(_prepare_key _changed_key _ready)
if(_ready)
    message(FATAL_ERROR "Android identity must reject missing NDK and API inputs")
endif()
set(ENGINE_V8_ANDROID_NDK "${_engine_root}/android-ndk")
set(ENGINE_V8_ANDROID_API 28)
if(CMAKE_HOST_WIN32)
    set(_ndk_host windows-x86_64)
    set(_ndk_tool_suffix .exe)
    write_fixture("${_source}/third_party/llvm-build/Release+Asserts/bin/clang.exe"
                  "fixture-input\n")
    write_fixture("${_source}/third_party/llvm-build/Release+Asserts/bin/llvm-ar.exe"
                  "fixture-input\n")
elseif(CMAKE_HOST_APPLE)
    set(_ndk_host darwin-x86_64)
    set(_ndk_tool_suffix "")
else()
    set(_ndk_host linux-x86_64)
    set(_ndk_tool_suffix "")
endif()
set(_ndk_files "${ENGINE_V8_ANDROID_NDK}/source.properties"
               "${ENGINE_V8_ANDROID_NDK}/meta/platforms.json")
foreach(_tool IN ITEMS clang llvm-ar ld.lld)
    list(
        APPEND
        _ndk_files
        "${ENGINE_V8_ANDROID_NDK}/toolchains/llvm/prebuilt/${_ndk_host}/bin/${_tool}${_ndk_tool_suffix}"
    )
endforeach()
foreach(_file IN LISTS _ndk_files)
    write_fixture("${_file}" "fixture-ndk\n")
endforeach()
_engine_v8_cache_keys(_prepare_key _android_key _ready)
if(NOT _ready OR _android_key STREQUAL _native_key)
    message(FATAL_ERROR "Android target must not reuse the native cache identity")
endif()
set(ENGINE_V8_ANDROID_API 29)
_engine_v8_cache_keys(_prepare_key _changed_key _ready)
if(NOT _ready OR _changed_key STREQUAL _android_key)
    message(FATAL_ERROR "Changed Android API must invalidate the cache")
endif()
set(ENGINE_V8_ANDROID_API invalid)
_engine_v8_cache_keys(_prepare_key _changed_key _ready)
if(_ready)
    message(FATAL_ERROR "Invalid Android API must reject the prepared workspace")
endif()
set(ENGINE_V8_ANDROID_API 28)
foreach(_file IN LISTS _ndk_files)
    _engine_v8_cache_keys(_prepare_key _before_change _ready)
    write_fixture("${_file}" "changed-ndk-input\n")
    _engine_v8_cache_keys(_prepare_key _changed_key _ready)
    if(NOT _ready OR _changed_key STREQUAL _before_change)
        message(FATAL_ERROR "Changed NDK version or tool must invalidate the cache: ${_file}")
    endif()
    file(REMOVE "${_file}")
    _engine_v8_cache_keys(_prepare_key _changed_key _ready)
    if(_ready)
        message(FATAL_ERROR "Missing NDK version or tool must reject the cache: ${_file}")
    endif()
    write_fixture("${_file}" "fixture-ndk\n")
endforeach()
unset(ENGINE_V8_TARGET_OS)
unset(ENGINE_V8_ANDROID_NDK)
unset(ENGINE_V8_ANDROID_API)
set(ENGINE_V8_TARGET_CPU "${_saved_cpu}")

set(ENGINE_V8_ENABLE_I18N ON)
set(_artifacts "${_out}/obj/${_library}" "${_out}/icudtl.dat" "${_out}/gen/include/v8-gn.h"
               "${_sample}" "${_out}/build.ninja")
foreach(_artifact IN LISTS _artifacts)
    write_fixture("${_artifact}" "fixture-output\n")
endforeach()
write_fixture("${_out}/args.gn" "${_args}")
_engine_v8_artifacts(_present)
if(NOT _present)
    message(FATAL_ERROR "Complete nonempty output fixture must be accepted")
endif()
foreach(_artifact IN LISTS _artifacts)
    file(REMOVE "${_artifact}")
    _engine_v8_artifacts(_present)
    if(_present)
        message(FATAL_ERROR "Missing output must reject a cache hit: ${_artifact}")
    endif()
    write_fixture("${_artifact}" "")
    _engine_v8_artifacts(_present)
    if(_present)
        message(FATAL_ERROR "Empty output must reject a cache hit: ${_artifact}")
    endif()
    write_fixture("${_artifact}" "fixture-output\n")
endforeach()
set(ENGINE_V8_ENABLE_I18N OFF)
_engine_v8_artifacts(_present)
if(_present)
    message(FATAL_ERROR "V8 without internationalization must reject stale ICU generated data")
endif()
file(REMOVE "${_out}/icudtl.dat")
_engine_v8_artifacts(_present)
if(NOT _present)
    message(FATAL_ERROR "V8 without internationalization must not require ICU data")
endif()
set(ENGINE_V8_ENABLE_I18N ON)
_engine_v8_artifacts(_present)
if(_present)
    message(FATAL_ERROR "V8 with internationalization must still require ICU data")
endif()
write_fixture("${_out}/icudtl.dat" "fixture-output\n")
write_fixture("${_out}/args.gn" "is_debug = false\n")
_engine_v8_artifacts(_present)
if(_present)
    message(FATAL_ERROR "Changed GN arguments must reject a cache hit")
endif()
write_fixture("${_out}/args.gn" "${_args}")

set(_saved_sample "${_sample}")
set(_sample "")
set(ENGINE_V8_TARGET_OS android)
_engine_v8_artifacts(_present)
if(NOT _present)
    message(FATAL_ERROR "Android may cache library outputs without a requested sample")
endif()
set(_sample "${_out}/missing-android-sample")
_engine_v8_artifacts(_present)
if(_present)
    message(FATAL_ERROR "An explicitly requested Android sample must exist")
endif()
set(_sample "")
unset(ENGINE_V8_TARGET_OS)
_engine_v8_artifacts(_present)
if(_present)
    message(FATAL_ERROR "Native cache must still require its executable sample")
endif()
set(_sample "${_saved_sample}")

write_fixture("${_source}/include/v8.h" "public-header\n")
write_fixture("${ENGINE_V8_BUILD_ROOT}/include/v8/v8.h" "public-header\n")
write_fixture("${ENGINE_V8_BUILD_ROOT}/include/debug/v8-gn.h" "fixture-output\n")
_engine_v8_headers_current(_current)
if(NOT _current)
    message(FATAL_ERROR "Matching public and generated headers must be accepted")
endif()
file(REMOVE "${ENGINE_V8_BUILD_ROOT}/include/v8/v8.h")
_engine_v8_headers_current(_current)
if(_current)
    message(FATAL_ERROR "Missing copied public header must reject a cache hit")
endif()
write_fixture("${ENGINE_V8_BUILD_ROOT}/include/v8/v8.h" "wrong-header\n")
_engine_v8_headers_current(_current)
if(_current)
    message(FATAL_ERROR "Changed copied public header must reject a cache hit")
endif()
write_fixture("${ENGINE_V8_BUILD_ROOT}/include/v8/v8.h" "public-header\n")

# A static/reduced variant must use its own generated feature/ABI header, even
# when a complete default header and archive are present beside it.
set(_default_out "${_out}")
set(_config_lower "mt-debug-f0000")
set(_out "${_source}/out/engine.${_config_lower}")
write_fixture("${_out}/gen/include/v8-gn.h" "static-reduced-header\n")
_engine_v8_headers_current(_current)
if(_current)
    message(FATAL_ERROR "A variant must not fall back to the default generated header")
endif()
write_fixture("${ENGINE_V8_BUILD_ROOT}/include/${_config_lower}/v8-gn.h" "fixture-output\n")
_engine_v8_headers_current(_current)
if(_current)
    message(FATAL_ERROR "A header copied from another V8 variant must be rejected")
endif()
write_fixture("${ENGINE_V8_BUILD_ROOT}/include/${_config_lower}/v8-gn.h" "static-reduced-header\n")
_engine_v8_headers_current(_current)
if(NOT _current)
    message(FATAL_ERROR "The matching variant-specific generated header must be accepted")
endif()
set(_config_lower debug)
set(_out "${_default_out}")
file(REMOVE "${_stamp}")
_engine_v8_stamp_matches("${_stamp}" "${_original_key}" _matches)
if(_matches)
    message(FATAL_ERROR "Interrupted build without a success stamp must not hit")
endif()

file(REMOVE_RECURSE "${_engine_root}")
message(STATUS "V8 cache invalidation and missing/empty output checks passed")
