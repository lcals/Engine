# The checked system-STL route keeps the vendor ABI and upstream bounds checks.
# Its identity is independent of Debug/Release; Debug reuses the Release probe.
set(_libcxx_proof "${ENGINE_V8_BUILD_ROOT}/.engine-v8-system-libcxx.identity")
_engine_v8_stamp_matches("${_libcxx_proof}" "${_prepare_key}" _libcxx_verified)
if(_libcxx_verified)
    return()
endif()
if(NOT CMAKE_HOST_APPLE)
    message(FATAL_ERROR "V8 checked system libc++ currently requires a macOS host")
endif()

execute_process(COMMAND xcrun --sdk macosx --show-sdk-path
                RESULT_VARIABLE _libcxx_sdk_result OUTPUT_VARIABLE _libcxx_sdk
                OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_VARIABLE _libcxx_sdk_error)
execute_process(COMMAND xcrun --find clang++
                RESULT_VARIABLE _libcxx_xcode_result OUTPUT_VARIABLE _libcxx_xcode_clang
                OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_VARIABLE _libcxx_xcode_error)
if(NOT _libcxx_sdk_result EQUAL 0 OR NOT _libcxx_xcode_result EQUAL 0)
    message(FATAL_ERROR "Cannot locate the checked macOS SDK: ${_libcxx_sdk_error}${_libcxx_xcode_error}")
endif()
get_filename_component(_libcxx_xcode_bin "${_libcxx_xcode_clang}" DIRECTORY)
get_filename_component(_libcxx_xcode_usr "${_libcxx_xcode_bin}" DIRECTORY)
set(_libcxx_lto "${_libcxx_xcode_usr}/lib/libLTO.dylib")
if(NOT EXISTS "${_libcxx_lto}")
    message(FATAL_ERROR "The checked V8 system linker requires the Xcode libLTO library")
endif()
set(_libcxx_probe_root "${ENGINE_V8_BUILD_ROOT}/system-libcxx-probe")
file(MAKE_DIRECTORY "${_libcxx_probe_root}")
set(_libcxx_probe "${CMAKE_CURRENT_LIST_DIR}/v8_system_libcxx_probe.cpp")
set(_libcxx_guard "${_source}/build/config/c++/engine_system_libcxx_guard.h")
set(_libcxx_clang "${_source}/third_party/llvm-build/Release+Asserts/bin/clang++")
set(_libcxx_flags -std=c++23 -O2 -fno-exceptions -fno-rtti -Werror
    -D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE -include "${_libcxx_guard}")
set(_libcxx_executable "${_libcxx_probe_root}/macos-probe")
_engine_v8_run("${_source}" "${_libcxx_clang}" ${_libcxx_flags}
               -isysroot "${_libcxx_sdk}" "-Wl,-lto_library,${_libcxx_lto}"
               "${_libcxx_probe}" -o "${_libcxx_executable}")
execute_process(COMMAND "${_libcxx_executable}" RESULT_VARIABLE _libcxx_valid_result
                OUTPUT_VARIABLE _libcxx_valid_output ERROR_VARIABLE _libcxx_valid_error)
if(NOT _libcxx_valid_result STREQUAL "0")
    message(FATAL_ERROR "System libc++ valid-access probe failed: ${_libcxx_valid_error}")
endif()
foreach(_libcxx_case IN ITEMS vector span text)
    if(_libcxx_case STREQUAL "text")
        set(_libcxx_index 4)
    else()
        set(_libcxx_index 3)
    endif()
    execute_process(COMMAND "${_libcxx_executable}" "${_libcxx_case}" "${_libcxx_index}"
                    RESULT_VARIABLE _libcxx_trap_result OUTPUT_QUIET ERROR_QUIET)
    if(NOT _libcxx_trap_result MATCHES "^(SIGTRAP|SIGILL)$")
        message(FATAL_ERROR
            "System libc++ ${_libcxx_case} bounds check did not trap: ${_libcxx_trap_result}")
    endif()
endforeach()
if(_android)
    set(_libcxx_ndk_base "${ENGINE_V8_ANDROID_NDK}/toolchains/llvm/prebuilt/${_ndk_host}")
    _engine_v8_run("${_source}" "${_libcxx_clang}" ${_libcxx_flags}
                   "--target=aarch64-linux-android${ENGINE_V8_ANDROID_API}"
                   "--sysroot=${_libcxx_ndk_base}/sysroot" "-resource-dir=${_ndk_resource_dir}"
                   "-fuse-ld=${_source}/third_party/llvm-build/Release+Asserts/bin/ld.lld"
                   -static-libstdc++ "${_libcxx_probe}" -o "${_libcxx_probe_root}/android-probe")
endif()
_engine_v8_write_stamp("${_libcxx_proof}" "${_prepare_key}")
string(STRIP "${_libcxx_valid_output}" _libcxx_valid_output)
message(STATUS "Checked ${_libcxx_valid_output}; vector/span/string bounds traps passed")
