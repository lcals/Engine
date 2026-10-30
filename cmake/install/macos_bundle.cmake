# 修正已组装 .app 的程序依赖；项目资源与 USD registry/data 由调用方复制。
cmake_minimum_required(VERSION 3.25)

if(NOT CMAKE_HOST_APPLE)
    message(FATAL_ERROR "macOS bundle fixup requires a macOS host")
endif()
if(NOT DEFINED ENGINE_APP_BUNDLE OR NOT IS_DIRECTORY "${ENGINE_APP_BUNDLE}")
    message(FATAL_ERROR "ENGINE_APP_BUNDLE must name an existing .app directory")
endif()
file(REAL_PATH "${ENGINE_APP_BUNDLE}" ENGINE_APP_BUNDLE)
if(NOT ENGINE_APP_BUNDLE MATCHES "\\.app$")
    message(FATAL_ERROR "ENGINE_APP_BUNDLE must have the .app suffix")
endif()

include(BundleUtilities)
get_bundle_and_executable("${ENGINE_APP_BUNDLE}" _bundle _executable _valid)
if(NOT _valid)
    message(FATAL_ERROR "The app must contain Info.plist and its CFBundleExecutable")
endif()
file(REAL_PATH "${_executable}" _real_executable)
cmake_path(IS_PREFIX ENGINE_APP_BUNDLE "${_real_executable}" NORMALIZE _inside_bundle)
if(NOT _inside_bundle)
    message(FATAL_ERROR "The app executable must not point outside its bundle")
endif()

set(_search_directories)
foreach(_directory IN LISTS ENGINE_LIBRARY_DIRS)
    if(NOT IS_DIRECTORY "${_directory}")
        message(FATAL_ERROR "Library search directory does not exist: ${_directory}")
    endif()
    file(REAL_PATH "${_directory}" _directory)
    list(APPEND _search_directories "${_directory}")
endforeach()
list(REMOVE_DUPLICATES _search_directories)

# 动态加载的插件不一定出现在主程序 otool -L 中，调用方须先放入 app。
file(
    GLOB_RECURSE _plugins
    LIST_DIRECTORIES FALSE
    "${ENGINE_APP_BUNDLE}/Contents/*.dylib" "${ENGINE_APP_BUNDLE}/Contents/*.so")
set(_plugin_libraries ${_plugins})
get_bundle_all_executables("${ENGINE_APP_BUNDLE}" _executables)
foreach(_tool IN LISTS _executables)
    if(NOT _tool STREQUAL _executable)
        list(APPEND _plugins "${_tool}")
    endif()
endforeach()
list(REMOVE_DUPLICATES _plugins)
find_program(_install_name_tool install_name_tool REQUIRED)
foreach(_tool IN LISTS _executables)
    set(_tool_rpaths)
    get_item_rpaths("${_tool}" _tool_rpaths)
    foreach(_directory IN LISTS _search_directories)
        if(NOT _directory IN_LIST _tool_rpaths)
            execute_process(COMMAND "${_install_name_tool}" -add_rpath "${_directory}" "${_tool}"
                                    COMMAND_ERROR_IS_FATAL ANY)
        endif()
    endforeach()
endforeach()
# BundleUtilities 会把递归依赖平铺后重新解析，丢失嵌套库的 @loader_path 上下文。
# 先用现代 Mach-O 扫描器收集真实闭包目录，再交给标准 bundle 修复和验证。
set(CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM "macos+macho")
file(
    GET_RUNTIME_DEPENDENCIES
    EXECUTABLES
    ${_executables}
    LIBRARIES
    ${_plugin_libraries}
    RESOLVED_DEPENDENCIES_VAR
    _resolved
    UNRESOLVED_DEPENDENCIES_VAR
    _unresolved
    DIRECTORIES
    ${_search_directories}
    POST_EXCLUDE_REGEXES
    "^/System/Library/"
    "^/usr/lib/")
if(_unresolved)
    message(FATAL_ERROR "Cannot resolve app dependencies: ${_unresolved}")
endif()
foreach(_dependency IN LISTS _resolved)
    cmake_path(GET _dependency PARENT_PATH _directory)
    list(APPEND _search_directories "${_directory}")
endforeach()
list(REMOVE_DUPLICATES _search_directories)
foreach(_plugin IN LISTS _plugins)
    file(REAL_PATH "${_plugin}" _real_plugin)
    cmake_path(IS_PREFIX ENGINE_APP_BUNDLE "${_real_plugin}" NORMALIZE _inside_bundle)
    if(NOT _inside_bundle)
        message(FATAL_ERROR "Bundled libraries must not point outside the app: ${_plugin}")
    endif()
endforeach()

set(BU_CHMOD_BUNDLE_ITEMS TRUE)
set(BU_COPY_FULL_FRAMEWORK_CONTENTS TRUE)
fixup_bundle("${ENGINE_APP_BUNDLE}" "${_plugins}" "${_search_directories}")

# BundleUtilities 已将依赖重写为包内路径，并删除原 LC_RPATH；再次验证，
# 防止把机器上的构建目录、Homebrew 或 vcpkg 搜索路径带入交付物。
get_bundle_all_executables("${ENGINE_APP_BUNDLE}" _binaries)
file(
    GLOB_RECURSE _libraries
    LIST_DIRECTORIES FALSE
    "${ENGINE_APP_BUNDLE}/Contents/*.dylib" "${ENGINE_APP_BUNDLE}/Contents/*.so")
list(APPEND _binaries "${_executable}" ${_libraries})
set(_real_binaries)
foreach(_binary IN LISTS _binaries)
    file(REAL_PATH "${_binary}" _real_binary)
    cmake_path(IS_PREFIX ENGINE_APP_BUNDLE "${_real_binary}" NORMALIZE _inside_bundle)
    if(NOT _inside_bundle)
        message(FATAL_ERROR "A fixed-up binary resolves outside its bundle: ${_binary}")
    endif()
    list(APPEND _real_binaries "${_real_binary}")
endforeach()
list(REMOVE_DUPLICATES _real_binaries)
find_program(_codesign codesign REQUIRED)
foreach(_binary IN LISTS _real_binaries)
    set(_rpaths)
    get_item_rpaths("${_binary}" _rpaths)
    foreach(_rpath IN LISTS _rpaths)
        if(IS_ABSOLUTE "${_rpath}")
            message(FATAL_ERROR "Absolute runtime search path remains in ${_binary}: ${_rpath}")
        endif()
    endforeach()
    # 对主程序执行 codesign 等同于签整个 app，必须等所有嵌套代码处理后再做。
    if(NOT _binary STREQUAL _real_executable)
        execute_process(COMMAND "${_codesign}" --force --sign - --timestamp=none "${_binary}"
                                COMMAND_ERROR_IS_FATAL ANY)
    endif()
endforeach()
# 本地产物使用 ad-hoc 签名；发布证书与公证由发布流程另行提供。
execute_process(COMMAND "${_codesign}" --force --deep --sign - --timestamp=none
                        "${ENGINE_APP_BUNDLE}" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${_codesign}" --verify --deep --strict --verbose=2 "${ENGINE_APP_BUNDLE}"
                        COMMAND_ERROR_IS_FATAL ANY)
verify_app("${ENGINE_APP_BUNDLE}")
message(STATUS "Verified self-contained macOS application: ${ENGINE_APP_BUNDLE}")
