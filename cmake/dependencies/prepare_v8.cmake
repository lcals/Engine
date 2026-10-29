# 显式准备固定版本的 V8；CHECK 只读，PREPARE 才会下载依赖，BUILD 只编译。
cmake_minimum_required(VERSION 3.25)

get_filename_component(_engine_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
if(NOT DEFINED ENGINE_V8_ACTION)
    set(ENGINE_V8_ACTION CHECK)
endif()
if(NOT ENGINE_V8_ACTION MATCHES "^(CHECK|PREPARE|BUILD)$")
    message(FATAL_ERROR "ENGINE_V8_ACTION must be CHECK, PREPARE or BUILD")
endif()
if(NOT DEFINED ENGINE_V8_BUILD_ROOT)
    if(ENGINE_V8_TARGET_OS STREQUAL "android")
        set(ENGINE_V8_BUILD_ROOT "${_engine_root}/build/v8/android-arm64")
    else()
        set(ENGINE_V8_BUILD_ROOT "${_engine_root}/build/v8")
    endif()
endif()
if(NOT DEFINED ENGINE_V8_CONFIG)
    set(ENGINE_V8_CONFIG Release)
endif()
if(NOT ENGINE_V8_CONFIG MATCHES "^(Debug|Release)$")
    message(FATAL_ERROR "ENGINE_V8_CONFIG must be Debug or Release")
endif()
if(NOT DEFINED ENGINE_V8_JOBS)
    set(ENGINE_V8_JOBS 4)
endif()
if(NOT ENGINE_V8_JOBS MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "ENGINE_V8_JOBS must be a positive integer")
endif()
set(_android FALSE)
if(ENGINE_V8_TARGET_OS STREQUAL "android")
    set(_android TRUE)
    if(NOT EXISTS "${ENGINE_V8_ANDROID_NDK}/source.properties")
        message(
            FATAL_ERROR "Android V8 requires ENGINE_V8_ANDROID_NDK pointing to NDK 27.2.12479018")
    endif()
    file(READ "${ENGINE_V8_ANDROID_NDK}/source.properties" _ndk_properties)
    if(NOT _ndk_properties MATCHES "Pkg.Revision[ \t]*=[ \t]*27\\.2\\.12479018")
        message(
            FATAL_ERROR "Android V8 currently uses NDK 27.2.12479018, matching the APK toolchain")
    endif()
    if(NOT ENGINE_V8_ANDROID_API MATCHES "^[0-9]+$" OR ENGINE_V8_ANDROID_API LESS 28)
        message(FATAL_ERROR "Android V8 requires ENGINE_V8_ANDROID_API >= 28")
    endif()
    file(REAL_PATH "${ENGINE_V8_ANDROID_NDK}" ENGINE_V8_ANDROID_NDK)
    if(CMAKE_HOST_APPLE)
        set(_ndk_host darwin-x86_64)
    elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
        set(_ndk_host linux-x86_64)
    else()
        message(FATAL_ERROR "Android V8 preparation currently requires a Linux or macOS host")
    endif()
    set(_ndk_resource_dir
        "${ENGINE_V8_ANDROID_NDK}/toolchains/llvm/prebuilt/${_ndk_host}/lib/clang/18")
    if(NOT EXISTS "${_ndk_resource_dir}/lib/linux/libclang_rt.builtins-aarch64-android.a")
        message(FATAL_ERROR "Android V8 is missing the NDK ARM64 compiler runtime")
    endif()
endif()
# 脚本模式不会初始化 CMAKE_HOST_SYSTEM_PROCESSOR。
cmake_host_system_information(RESULT _host_processor QUERY OS_PLATFORM)
if(_host_processor MATCHES "^(AMD64|amd64|x86_64|x64)$")
    set(_host_cpu x64)
elseif(CMAKE_HOST_APPLE AND _host_processor MATCHES "^(arm64|aarch64)$")
    set(_host_cpu arm64)
else()
    message(
        FATAL_ERROR "The V8 preparation workflow supports native x64 and macOS ARM64 builds only")
endif()
if(NOT DEFINED ENGINE_V8_TARGET_CPU)
    set(ENGINE_V8_TARGET_CPU "${_host_cpu}")
elseif(NOT _android AND NOT ENGINE_V8_TARGET_CPU STREQUAL _host_cpu)
    message(
        FATAL_ERROR
            "V8 target CPU ${ENGINE_V8_TARGET_CPU} differs from the native host CPU ${_host_cpu}")
endif()
if(_android AND NOT ENGINE_V8_TARGET_CPU STREQUAL "arm64")
    message(FATAL_ERROR "The Android V8 integration currently requires target CPU arm64")
endif()

cmake_path(ABSOLUTE_PATH ENGINE_V8_BUILD_ROOT NORMALIZE)
set(_allowed_root "${_engine_root}/build")
cmake_path(IS_PREFIX _allowed_root "${ENGINE_V8_BUILD_ROOT}" NORMALIZE _inside_build)
if(NOT _inside_build OR ENGINE_V8_BUILD_ROOT STREQUAL _allowed_root)
    message(FATAL_ERROR "ENGINE_V8_BUILD_ROOT must be inside the repository build directory")
endif()
if(EXISTS "${ENGINE_V8_BUILD_ROOT}")
    file(REAL_PATH "${ENGINE_V8_BUILD_ROOT}" _resolved_root)
    file(REAL_PATH "${_allowed_root}" _resolved_allowed)
    cmake_path(IS_PREFIX _resolved_allowed "${_resolved_root}" NORMALIZE _inside_real_build)
    if(NOT _inside_real_build OR _resolved_root STREQUAL _resolved_allowed)
        message(FATAL_ERROR "Resolved V8 build root escapes the repository build directory")
    endif()
endif()

set(_upstream "${_engine_root}/third_party/v8")
set(_depot "${ENGINE_V8_BUILD_ROOT}/depot_tools")
set(_workspace "${ENGINE_V8_BUILD_ROOT}/source")
set(_source "${_workspace}/v8")
if(CMAKE_HOST_WIN32)
    set(_patch_roots "${_source}/build" "${_source}" "${_source}/third_party/partition_alloc")
    set(_patch_files
        "${_engine_root}/third_party/overrides/v8/windows-dynamic-crt.patch"
        "${_engine_root}/third_party/overrides/v8/windows-shared-ptr-atomic-warning.patch"
        "${_engine_root}/third_party/overrides/v8/windows-debug-quarantine.patch")
elseif(CMAKE_HOST_APPLE)
    set(_patch_roots "${_source}/build" "${_source}/build" "${_source}")
    set(_patch_files
        "${_engine_root}/third_party/overrides/v8/macos-system-lto-library.patch"
        "${_engine_root}/third_party/overrides/v8/macos-system-math.patch"
        "${_engine_root}/third_party/overrides/v8/macos-signpost-attribute.patch")
endif()
if(_android)
    set(_android_deps_patch
        "${_engine_root}/third_party/overrides/v8/android-external-toolchain.patch")
    list(
        APPEND
        _patch_roots
        "${_source}/build"
        "${_source}/build"
        "${_source}"
        "${_source}"
        "${_source}/third_party/simdutf")
    list(
        APPEND
        _patch_files
        "${_engine_root}/third_party/overrides/v8/android-macos-host.patch"
        "${_engine_root}/third_party/overrides/v8/android-ndk-runtime.patch"
        "${_android_deps_patch}"
        "${_engine_root}/third_party/overrides/v8/android-atomic-ref.patch"
        "${_engine_root}/third_party/overrides/v8/android-simdutf-atomic-ref.patch")
endif()
string(TOLOWER "${ENGINE_V8_CONFIG}" _config_lower)
set(_out "${_source}/out/engine.${_config_lower}")
find_package(Git REQUIRED)
execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${_engine_root}" rev-parse "HEAD:third_party/v8"
    OUTPUT_VARIABLE _revision
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _revision_result)
if(NOT _revision_result EQUAL 0 OR NOT EXISTS "${_upstream}/DEPS")
    message(FATAL_ERROR "Initialize the fixed V8 submodule before preparing V8")
endif()
execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${_upstream}" rev-parse HEAD
    OUTPUT_VARIABLE _upstream_revision
    OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
if(NOT _upstream_revision STREQUAL _revision)
    message(FATAL_ERROR "V8 submodule HEAD differs from the parent repository's fixed revision")
endif()
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${_upstream}" show "${_revision}:DEPS"
                OUTPUT_VARIABLE _deps COMMAND_ERROR_IS_FATAL ANY)
string(REGEX MATCH "'third_party/depot_tools':[ \r\n]*[^\n]*'@'[ \t]*\\+[ \t]*'([0-9a-f]+)'"
             _depot_match "${_deps}")
if(NOT _depot_match)
    message(FATAL_ERROR "Cannot read the pinned depot_tools revision from V8 DEPS")
endif()
set(_depot_revision "${CMAKE_MATCH_1}")
string(REGEX MATCH "'build':[ \r\n]*[^\n]*'@'[ \t]*\\+[ \t]*'([0-9a-f]+)'" _build_match "${_deps}")
if(NOT _build_match)
    message(FATAL_ERROR "Cannot read the pinned Chromium build revision from V8 DEPS")
endif()
set(_build_revision "${CMAKE_MATCH_1}")
string(REGEX MATCH "'partition_alloc_version':[ \t]*'([0-9a-f]+)'" _partition_match "${_deps}")
if(NOT _partition_match)
    message(FATAL_ERROR "Cannot read the pinned PartitionAlloc revision from V8 DEPS")
endif()
set(_partition_revision "${CMAKE_MATCH_1}")
if(_android)
    string(REGEX MATCH "'third_party/simdutf':[ \r\n]*[^\n]*'@'[ \t]*\\+[ \t]*'([0-9a-f]+)'"
                 _simdutf_match "${_deps}")
    if(NOT _simdutf_match)
        message(FATAL_ERROR "Cannot read the pinned simdutf revision from V8 DEPS")
    endif()
    set(_simdutf_revision "${CMAKE_MATCH_1}")
endif()
message(STATUS "V8 ${_revision}; depot_tools ${_depot_revision}; Chromium build ${_build_revision}")
message(STATUS "V8 work copy: ${_source}; output: ${_out}")

if(ENGINE_V8_ACTION STREQUAL "CHECK")
    if(CMAKE_HOST_WIN32)
        set(_check_compiler clang-cl.exe)
        set(_check_library engine_v8.lib)
    else()
        set(_check_compiler clang)
        set(_check_library libengine_v8.a)
    endif()
    foreach(
        _path IN
        ITEMS "${_depot}/gclient.py" "${_source}/build/config/BUILDCONFIG.gn"
              "${_source}/third_party/llvm-build/Release+Asserts/bin/${_check_compiler}"
              "${_out}/gen/include/v8-gn.h" "${_out}/obj/${_check_library}")
        if(EXISTS "${_path}")
            message(STATUS "Present: ${_path}")
        else()
            message(STATUS "Missing: ${_path}")
        endif()
    endforeach()
endif()

if(CMAKE_HOST_WIN32)
    set(_gn "${_source}/buildtools/win/gn.exe")
    set(_ninja "${_source}/third_party/ninja/ninja.exe")
elseif(CMAKE_HOST_APPLE)
    set(_gn "${_source}/buildtools/mac/gn")
    set(_ninja "${_source}/third_party/ninja/ninja")
else()
    set(_gn "${_source}/buildtools/linux64/gn")
    set(_ninja "${_source}/third_party/ninja/ninja")
endif()
if(CMAKE_HOST_WIN32 AND NOT _android)
    set(_library engine_v8.lib)
    set(_sample "${_out}/v8_hello_world.exe")
else()
    set(_library libengine_v8.a)
    set(_sample "${_out}/v8_hello_world")
endif()
set(_args
    "target_cpu = \"${ENGINE_V8_TARGET_CPU}\"\nis_component_build = false\nv8_monolithic = true\nv8_use_external_startup_data = false\nuse_custom_libcxx = false\nuse_custom_libcxx_for_host = false\nv8_generate_external_defines_header = true\nuse_remoteexec = false\nuse_siso = false\nsymbol_level = 0\n"
)
if(CMAKE_HOST_APPLE AND NOT _android)
    # 系统 SDK 的模块和 TAPI 格式可能新于固定 LLVM；使用常规头文件及 Xcode 链接器。
    string(APPEND _args "use_clang_modules = false\nuse_lld = false\n")
endif()
if(_android)
    # GN 仍使用固定的宿主 Clang/Rust；Android ABI、sysroot 和运行库与 APK NDK 一致。
    string(APPEND _args "target_os = \"android\"\nv8_target_cpu = \"arm64\"\n")
    string(APPEND _args
           "android_ndk_root = \"${ENGINE_V8_ANDROID_NDK}\"\nandroid_ndk_version = \"r27c\"\n")
    string(
        APPEND
        _args
        "android_ndk_api_level = ${ENGINE_V8_ANDROID_API}\ndefault_min_sdk_version = ${ENGINE_V8_ANDROID_API}\n"
    )
    string(APPEND _args "android_clang_resource_dir = \"${_ndk_resource_dir}\"\n")
    string(
        APPEND
        _args
        "use_lld = true\nuse_clang_modules = false\nuse_thin_lto = false\nuse_custom_libunwind = false\n"
    )
    string(APPEND _args "v8_monolithic_for_shared_library = true\n")
endif()
if(CMAKE_HOST_WIN32
   OR CMAKE_HOST_APPLE
   OR _android)
    # 使用宿主 CRT 分配器，避免全局 malloc/new 替换及系统 libc++ 声明冲突。
    string(APPEND _args "use_allocator_shim = false\nuse_partition_alloc_as_malloc = false\n")
endif()
if(ENGINE_V8_CONFIG STREQUAL "Debug")
    string(APPEND _args "is_debug = true\n")
    if(CMAKE_HOST_WIN32)
        # Chromium 默认关闭此项；MSVC/vcpkg 的 /MDd 使用迭代器调试级别 2。
        # 在 V8 内启用相同 ABI，不向其他目标传播会冲突的 STL 禁用宏。
        string(APPEND _args "enable_iterator_debugging = true\n")
    endif()
else()
    string(APPEND _args "is_debug = false\n")
endif()
set(_prepare_stamp "${ENGINE_V8_BUILD_ROOT}/.engine-v8-prepared.identity")
set(_configure_stamp "${_out}/.engine-v8-configured.identity")
set(_build_stamp "${_out}/.engine-v8-build.identity")

if(NOT ENGINE_V8_ACTION STREQUAL "CHECK")
    file(MAKE_DIRECTORY "${ENGINE_V8_BUILD_ROOT}")
    file(
        LOCK "${ENGINE_V8_BUILD_ROOT}/.engine-v8.lock"
        GUARD PROCESS
        TIMEOUT 0
        RESULT_VARIABLE _lock_result)
    if(NOT _lock_result STREQUAL "0")
        message(FATAL_ERROR "Another V8 preparation/build is active: ${_lock_result}")
    endif()
endif()

function(_engine_v8_run _directory)
    execute_process(
        COMMAND ${ARGN}
        WORKING_DIRECTORY "${_directory}"
        RESULT_VARIABLE _result)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "V8 command failed (${_result}): ${ARGN}")
    endif()
endfunction()

function(_engine_v8_assert_revision _directory _expected)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${_directory}" rev-parse HEAD
        OUTPUT_VARIABLE _actual
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _result)
    if(NOT _result EQUAL 0 OR NOT _actual STREQUAL _expected)
        message(
            FATAL_ERROR
                "Unexpected revision in ${_directory}; expected ${_expected}. Use a new build root rather than resetting local changes."
        )
    endif()
endfunction()

function(_engine_v8_patch _directory _patch _action)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${_directory}" apply --reverse --check "${_patch}"
        RESULT_VARIABLE _already_patched
        OUTPUT_QUIET ERROR_QUIET)
    if(_action STREQUAL "REMOVE")
        if(_already_patched EQUAL 0)
            # 只撤销已知补丁，保留副本中的其他修改；供 gclient 检查干净状态。
            _engine_v8_run("${_directory}" "${GIT_EXECUTABLE}" apply --reverse "${_patch}")
        else()
            # 未应用和不再适用必须区分，避免静默同步带有未知修改的工作副本。
            _engine_v8_run("${_directory}" "${GIT_EXECUTABLE}" apply --check "${_patch}")
        endif()
    elseif(NOT _already_patched EQUAL 0)
        _engine_v8_run("${_directory}" "${GIT_EXECUTABLE}" apply --check "${_patch}")
        _engine_v8_run("${_directory}" "${GIT_EXECUTABLE}" apply "${_patch}")
    endif()
endfunction()

# 缓存核对与后续构建使用同一份隔离 Git 配置，避免用户 diff 配置影响身份。
if(EXISTS "${ENGINE_V8_BUILD_ROOT}/.gitconfig")
    set(ENV{GIT_CONFIG_GLOBAL} "${ENGINE_V8_BUILD_ROOT}/.gitconfig")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/v8_cache.cmake")
_engine_v8_cache_keys(_prepare_key _build_key _cache_ready)
_engine_v8_artifacts(_artifacts_present)
if(ENGINE_V8_ACTION STREQUAL "CHECK")
    message(
        STATUS
            "V8 workspace identity valid: ${_cache_ready}; outputs present: ${_artifacts_present}")
    if(_cache_ready)
        _engine_v8_stamp_matches("${_build_stamp}" "${_build_key}" _built)
        message(STATUS "V8 build cache matches these inputs: ${_built}")
    endif()
    return()
endif()
set(_reuse_prepared FALSE)
if(_cache_ready)
    _engine_v8_stamp_matches("${_prepare_stamp}" "${_prepare_key}" _prepared)
    _engine_v8_stamp_matches("${_configure_stamp}" "${_build_key}" _configured)
    _engine_v8_stamp_matches("${_build_stamp}" "${_build_key}" _built)
    if(ENGINE_V8_ACTION STREQUAL "BUILD"
       AND _built
       AND _artifacts_present)
        _engine_v8_headers_current(_headers_current)
        if(_headers_current)
            message(STATUS "V8 ${ENGINE_V8_CONFIG} cache hit; reusing verified library and headers")
            return()
        endif()
    endif()
    if(ENGINE_V8_ACTION STREQUAL "PREPARE")
        # 旧工作区也先核对提交、补丁、参数、完整工具链及已有构建产物。
        # 只复用准备结果；没有 BUILD 成功戳仍须经过 Ninja 和样例确认。
        if(_prepared OR _artifacts_present)
            set(_reuse_prepared TRUE)
        endif()
        if(_prepared
           AND _configured
           AND EXISTS "${_out}/build.ninja"
           AND EXISTS "${_out}/args.gn")
            file(READ "${_out}/args.gn" _saved_args)
            if(_saved_args STREQUAL _args)
                message(STATUS "V8 ${ENGINE_V8_CONFIG} preparation cache hit; no sync or hooks")
                return()
            endif()
        endif()
        if(_reuse_prepared)
            message(STATUS "Reusing verified V8 workspace without removing or reapplying patches")
        endif()
    endif()
endif()

if(ENGINE_V8_ACTION STREQUAL "BUILD")
    # 重新确认之前先撤销旧成功证明，失败或中断后不能复用残缺产物。
    file(REMOVE "${_build_stamp}")
endif()

# depot_tools 的 Windows bootstrap 会调用 git config --global；隔离到构建目录，
# 同时保留本流程需要的长路径和 LF 检出设置，不改变用户全局 Git 配置。
if(NOT EXISTS "${ENGINE_V8_BUILD_ROOT}/.gitconfig")
    file(WRITE "${ENGINE_V8_BUILD_ROOT}/.gitconfig"
         "[core]\n\tlongpaths = true\n\tautocrlf = false\n\tfilemode = false\n")
endif()
set(ENV{GIT_CONFIG_GLOBAL} "${ENGINE_V8_BUILD_ROOT}/.gitconfig")
set(ENV{GIT_CACHE_PATH} "${ENGINE_V8_BUILD_ROOT}/git-cache")

if(ENGINE_V8_ACTION STREQUAL "PREPARE" AND NOT _reuse_prepared)
    if(NOT EXISTS "${_depot}/.git")
        file(MAKE_DIRECTORY "${_depot}")
        _engine_v8_run("${_depot}" "${GIT_EXECUTABLE}" init)
    endif()
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${_depot}" remote get-url origin
        RESULT_VARIABLE _depot_remote_result
        OUTPUT_QUIET ERROR_QUIET)
    if(NOT _depot_remote_result EQUAL 0)
        _engine_v8_run("${_depot}" "${GIT_EXECUTABLE}" remote add origin
                       https://chromium.googlesource.com/chromium/tools/depot_tools.git)
    endif()
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${_depot}" rev-parse --verify HEAD
        RESULT_VARIABLE _depot_head_result
        OUTPUT_QUIET ERROR_QUIET)
    if(NOT _depot_head_result EQUAL 0)
        # 初始化后下载中断时，下一次继续获取同一固定提交，不重置已有检出。
        _engine_v8_run(
            "${_depot}"
            "${GIT_EXECUTABLE}"
            -c
            core.autocrlf=false
            fetch
            --depth=1
            origin
            "${_depot_revision}")
        _engine_v8_run(
            "${_depot}"
            "${GIT_EXECUTABLE}"
            -c
            core.autocrlf=false
            checkout
            --detach
            FETCH_HEAD)
    endif()
    _engine_v8_assert_revision("${_depot}" "${_depot_revision}")
    file(MAKE_DIRECTORY "${_workspace}")
    if(NOT EXISTS "${_source}/.git")
        _engine_v8_run(
            "${_workspace}"
            "${GIT_EXECUTABLE}"
            -c
            core.autocrlf=false
            clone
            --local
            --no-hardlinks
            --no-checkout
            "${_upstream}"
            "${_source}")
        _engine_v8_run("${_source}" "${GIT_EXECUTABLE}" remote set-url origin
                       https://chromium.googlesource.com/v8/v8.git)
        _engine_v8_run(
            "${_source}"
            "${GIT_EXECUTABLE}"
            -c
            core.autocrlf=false
            checkout
            --detach
            "${_revision}")
    endif()
    _engine_v8_assert_revision("${_source}" "${_revision}")
    # clone --no-checkout 之后中断时补齐检出；不使用 --force，保护副本中的额外修改。
    _engine_v8_run(
        "${_source}"
        "${GIT_EXECUTABLE}"
        -c
        core.autocrlf=false
        checkout
        --detach
        "${_revision}")
    set(_gclient_config
        [=[solutions = [{
  "name": "v8",
  "url": "https://chromium.googlesource.com/v8/v8.git",
  "managed": False,
  "deps_file": "DEPS",
  "custom_deps": {
    "v8/test/benchmarks/data": None,
    "v8/test/mozilla/data": None,
    "v8/test/test262/data": None,
  },
  "custom_vars": {},
}]
target_os = []
]=])
    if(_android)
        # 使用 APK 的外部 NDK；原生 V8 不需要 Chromium 的 Java/测试工具。
        string(REPLACE "target_os = []" "target_os = ['android']" _gclient_config
                       "${_gclient_config}")
        string(REPLACE "\"custom_vars\": {}"
                       "\"custom_vars\": {\"checkout_android_toolchain\": False}" _gclient_config
                       "${_gclient_config}")
        string(
            REPLACE
                "\"custom_deps\": {"
                [=["custom_deps": {
    "v8/third_party/android_platform": None,
    "v8/third_party/colorama/src": None,
    "v8/third_party/requests": None,]=]
                _gclient_config
                "${_gclient_config}")
    endif()
    if(EXISTS "${_workspace}/.gclient")
        file(READ "${_workspace}/.gclient" _existing_gclient)
        if(NOT _existing_gclient STREQUAL _gclient_config)
            message(
                FATAL_ERROR
                    "Existing ${_workspace}/.gclient differs from the engine configuration; inspect it before replacing it"
            )
        endif()
    else()
        file(WRITE "${_workspace}/.gclient" "${_gclient_config}")
    endif()
else()
    if(NOT EXISTS "${_source}/build/config/BUILDCONFIG.gn")
        message(
            FATAL_ERROR
                "V8 is not prepared. Run the engine_v8_prepare target or this script with ENGINE_V8_ACTION=PREPARE first."
        )
    endif()
    _engine_v8_assert_revision("${_depot}" "${_depot_revision}")
    _engine_v8_assert_revision("${_source}" "${_revision}")
endif()

# 只影响当前进程及其子进程，不写入用户或系统环境变量。
set(ENV{DEPOT_TOOLS_UPDATE} 0)
if(CMAKE_HOST_WIN32)
    set(ENV{DEPOT_TOOLS_WIN_TOOLCHAIN} 0)
    # MSBuild 的 custom target 会注入包括未安装 ATL/MFC 路径的 INCLUDE。
    # Chromium 只在 VSINSTALLDIR 存在时才清理它；此处始终丢弃继承的工具链
    # 搜索路径和开发者命令行状态，由固定 setup_toolchain.py 重新执行 vcvarsall。
    # PATH、代理及显式 VS 安装位置选择保留，变更仅限当前脚本进程。
    foreach(
        _environment IN
        ITEMS INCLUDE
              LIB
              LIBPATH
              VSINSTALLDIR
              VCINSTALLDIR
              VCToolsInstallDir
              VCToolsVersion
              WindowsSdkDir
              WindowsSDKVersion
              WindowsSDKLibVersion
              UniversalCRTSdkDir
              UCRTVersion
              ExtensionSdkDir
              VSCMD_VER
              VSCMD_ARG_HOST_ARCH
              VSCMD_ARG_TGT_ARCH
              VSCMD_ARG_APP_PLAT
              __VSCMD_PREINIT_PATH
              __VSCMD_PREINIT_INCLUDE
              __VSCMD_PREINIT_LIB
              __VSCMD_PREINIT_LIBPATH
              __VSCMD_PREINIT_VCToolsInstallDir)
        unset(ENV{${_environment}})
    endforeach()
    set(ENV{PATH} "${_depot};$ENV{PATH}")
    set(_gclient "$ENV{COMSPEC}" /d /c call "${_depot}/gclient.bat")
else()
    set(ENV{PATH} "${_depot}:$ENV{PATH}")
    set(_gclient "${_depot}/gclient")
endif()
if(ENGINE_V8_ACTION STREQUAL "PREPARE" AND NOT _reuse_prepared)
    if(CMAKE_HOST_WIN32)
        # 固定 depot_tools 并禁止自更新后，仍须显式生成 git.bat 等启动器。
        _engine_v8_run("${_depot}" "$ENV{COMSPEC}" /d /c call "${_depot}/bootstrap/win_tools.bat")
    endif()
    if(_patch_files)
        if(EXISTS "${_source}/build/.git")
            _engine_v8_assert_revision("${_source}/build" "${_build_revision}")
        endif()
        if(EXISTS "${_source}/third_party/partition_alloc/.git")
            _engine_v8_assert_revision("${_source}/third_party/partition_alloc"
                                       "${_partition_revision}")
        endif()
        set(_removal_roots ${_patch_roots})
        set(_removal_files ${_patch_files})
        list(REVERSE _removal_roots)
        list(REVERSE _removal_files)
        foreach(_patch_root _patch_file IN ZIP_LISTS _removal_roots _removal_files)
            if(EXISTS "${_patch_root}/.git")
                _engine_v8_patch("${_patch_root}" "${_patch_file}" REMOVE)
            endif()
        endforeach()
    endif()
    if(_android)
        # CIPD 不遵循 Git 的路径级 custom_deps=None；解析 DEPS 前关闭外部工具链下载。
        _engine_v8_patch("${_source}" "${_android_deps_patch}" APPLY)
    endif()
    _engine_v8_run("${_workspace}" ${_gclient} sync --nohooks --no-history
                   "--jobs=${ENGINE_V8_JOBS}")
endif()
_engine_v8_assert_revision("${_source}/build" "${_build_revision}")
_engine_v8_assert_revision("${_source}/third_party/partition_alloc" "${_partition_revision}")
if(_android)
    _engine_v8_assert_revision("${_source}/third_party/simdutf" "${_simdutf_revision}")
endif()

# 按各自 Git 根目录应用嵌入方兼容补丁，原 V8 子模块保持只读。
if(_patch_files)
    foreach(_patch_root _patch_file IN ZIP_LISTS _patch_roots _patch_files)
        _engine_v8_patch("${_patch_root}" "${_patch_file}" APPLY)
    endforeach()
endif()
if(ENGINE_V8_ACTION STREQUAL "PREPARE" AND NOT _reuse_prepared)
    _engine_v8_run("${_workspace}" ${_gclient} runhooks "--jobs=${ENGINE_V8_JOBS}")
endif()

file(MAKE_DIRECTORY "${_out}")
if(EXISTS "${_out}/args.gn")
    file(READ "${_out}/args.gn" _existing_args)
    if(NOT _existing_args STREQUAL _args)
        message(
            FATAL_ERROR
                "Existing ${_out}/args.gn differs from the fixed engine arguments; use another build root or inspect and remove that args.gn"
        )
    endif()
else()
    file(WRITE "${_out}/args.gn" "${_args}")
endif()
if(NOT EXISTS "${_gn}" OR NOT EXISTS "${_ninja}")
    message(FATAL_ERROR "Pinned GN/Ninja are missing; rerun ENGINE_V8_ACTION=PREPARE")
endif()
_engine_v8_cache_keys(_prepare_key _build_key _cache_ready)
if(NOT _cache_ready)
    message(
        FATAL_ERROR
            "V8 workspace or toolchain validation failed; run engine_v8_prepare before building")
endif()
_engine_v8_stamp_matches("${_configure_stamp}" "${_build_key}" _configured)
if(NOT _configured OR NOT EXISTS "${_out}/build.ninja")
    _engine_v8_run("${_source}" "${_gn}" gen "${_out}")
    _engine_v8_write_stamp("${_configure_stamp}" "${_build_key}")
endif()
_engine_v8_write_stamp("${_prepare_stamp}" "${_prepare_key}")
file(MAKE_DIRECTORY "${ENGINE_V8_BUILD_ROOT}/include/v8")
file(COPY "${_source}/include/" DESTINATION "${ENGINE_V8_BUILD_ROOT}/include/v8")
if(ENGINE_V8_ACTION STREQUAL "BUILD")
    _engine_v8_run(
        "${_source}"
        "${_ninja}"
        -C
        "${_out}"
        -j
        "${ENGINE_V8_JOBS}"
        v8_monolith
        v8_hello_world)
    set(_package_arguments)
    if(_android)
        list(APPEND _package_arguments -DENGINE_V8_TARGET_OS=android
             "-DENGINE_V8_TARGET_CPU=${ENGINE_V8_TARGET_CPU}")
    endif()
    _engine_v8_run("${_out}" "${CMAKE_COMMAND}" "-DENGINE_V8_OUTPUT=${_out}" ${_package_arguments}
                   -P "${CMAKE_CURRENT_LIST_DIR}/package_v8.cmake")
    file(MAKE_DIRECTORY "${ENGINE_V8_BUILD_ROOT}/include/${_config_lower}")
    file(COPY_FILE "${_out}/gen/include/v8-gn.h"
         "${ENGINE_V8_BUILD_ROOT}/include/${_config_lower}/v8-gn.h" ONLY_IF_DIFFERENT)
    if(NOT _android)
        _engine_v8_run("${_out}" "${_sample}")
    endif()
    _engine_v8_artifacts(_artifacts_present)
    _engine_v8_headers_current(_headers_current)
    if(NOT _artifacts_present OR NOT _headers_current)
        message(
            FATAL_ERROR "V8 build finished without all required library, ICU, or header outputs")
    endif()
    _engine_v8_write_stamp("${_build_stamp}" "${_build_key}")
    message(STATUS "Recorded verified V8 ${ENGINE_V8_CONFIG} build cache")
endif()
