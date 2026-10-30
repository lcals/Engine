# 原生 Mach-O 可执行文件及离线工具部署；依赖集中在程序旁 lib/。
cmake_minimum_required(VERSION 3.25)

if(NOT CMAKE_HOST_APPLE)
    message(FATAL_ERROR "Native Mach-O deployment requires a macOS host")
endif()
if(NOT DEFINED ENGINE_EXECUTABLE OR NOT EXISTS "${ENGINE_EXECUTABLE}")
    message(FATAL_ERROR "ENGINE_EXECUTABLE must name an exported native executable")
endif()
file(REAL_PATH "${ENGINE_EXECUTABLE}" _executable)
if(IS_SYMLINK "${ENGINE_EXECUTABLE}")
    message(FATAL_ERROR "The exported executable must not be a symbolic link")
endif()
cmake_path(GET _executable PARENT_PATH _root)
set(_library_root "${_root}/lib")
set(_executables "${_executable}")
foreach(_tool IN ITEMS node shaderc)
    if(EXISTS "${_root}/tools/${_tool}")
        if(IS_SYMLINK "${_root}/tools/${_tool}")
            message(FATAL_ERROR "A deployed editor tool must not be a symbolic link: ${_tool}")
        endif()
        list(APPEND _executables "${_root}/tools/${_tool}")
    endif()
endforeach()
include(BundleUtilities)
find_program(_install_name_tool install_name_tool REQUIRED)
find_program(_codesign codesign REQUIRED)
# 复制后的 Homebrew 工具可能原本以 @loader_path/../lib 找到库；扫描时临时
# 补充原安装目录，完成重写后统一移除，交付物不依赖这些目录。
foreach(_binary IN LISTS _executables)
    set(_rpaths)
    get_item_rpaths("${_binary}" _rpaths)
    foreach(_directory IN LISTS ENGINE_LIBRARY_DIRS)
        if(NOT IS_DIRECTORY "${_directory}")
            message(FATAL_ERROR "Library search directory does not exist: ${_directory}")
        endif()
        if(NOT _directory IN_LIST _rpaths)
            execute_process(COMMAND "${_install_name_tool}" -add_rpath "${_directory}" "${_binary}"
                                    COMMAND_ERROR_IS_FATAL ANY)
        endif()
    endforeach()
endforeach()
set(CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM "macos+macho")
file(
    GET_RUNTIME_DEPENDENCIES
    EXECUTABLES
    ${_executables}
    RESOLVED_DEPENDENCIES_VAR
    _dependencies
    UNRESOLVED_DEPENDENCIES_VAR
    _unresolved
    CONFLICTING_DEPENDENCIES_PREFIX
    _conflicts
    DIRECTORIES
    ${ENGINE_LIBRARY_DIRS}
    POST_EXCLUDE_REGEXES
    "^/System/Library/"
    "^/usr/lib/")
if(_unresolved OR _conflicts_FILENAMES)
    message(
        FATAL_ERROR
            "Cannot deploy unresolved or conflicting native dependencies: ${_unresolved};${_conflicts_FILENAMES}"
    )
endif()
file(MAKE_DIRECTORY "${_library_root}")
set(_libraries)
foreach(_dependency IN LISTS _dependencies)
    cmake_path(GET _dependency FILENAME _filename)
    set(_destination "${_library_root}/${_filename}")
    file(REAL_PATH "${_dependency}" _source)
    if(NOT _source STREQUAL _destination)
        file(COPY_FILE "${_source}" "${_destination}" ONLY_IF_DIFFERENT)
    endif()
    file(
        CHMOD
        "${_destination}"
        PERMISSIONS
        OWNER_READ
        OWNER_WRITE
        OWNER_EXECUTE
        GROUP_READ
        GROUP_EXECUTE
        WORLD_READ
        WORLD_EXECUTE)
    list(APPEND _libraries "${_destination}")
endforeach()
list(REMOVE_DUPLICATES _libraries)
set(_binaries ${_libraries} ${_executables})
foreach(_binary IN LISTS _binaries)
    cmake_path(GET _binary PARENT_PATH _directory)
    # 无递归地读当前二进制的原始加载命令；按闭包中唯一库名重写。
    set(_prerequisites)
    get_prerequisites("${_binary}" _prerequisites 1 0 "${_directory}"
                      "${ENGINE_LIBRARY_DIRS};${_library_root}")
    set(_changes)
    foreach(_dependency IN LISTS _prerequisites)
        cmake_path(GET _dependency FILENAME _filename)
        set(_destination "${_library_root}/${_filename}")
        if(NOT _destination IN_LIST _libraries)
            message(FATAL_ERROR "Dependency absent from native closure: ${_binary}: ${_dependency}")
        endif()
        file(RELATIVE_PATH _relative "${_directory}" "${_destination}")
        list(APPEND _changes -change "${_dependency}" "@loader_path/${_relative}")
    endforeach()
    if(_binary IN_LIST _libraries)
        cmake_path(GET _binary FILENAME _filename)
        list(APPEND _changes -id "@rpath/${_filename}")
    endif()
    set(_rpaths)
    get_item_rpaths("${_binary}" _rpaths)
    foreach(_rpath IN LISTS _rpaths)
        list(APPEND _changes -delete_rpath "${_rpath}")
    endforeach()
    if(_changes)
        execute_process(COMMAND "${_install_name_tool}" ${_changes} "${_binary}"
                                COMMAND_ERROR_IS_FATAL ANY)
    endif()
    execute_process(COMMAND "${_codesign}" --force --sign - --timestamp=none "${_binary}"
                            COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND "${_codesign}" --verify --strict "${_binary}" COMMAND_ERROR_IS_FATAL
                            ANY)
endforeach()
file(
    GET_RUNTIME_DEPENDENCIES
    EXECUTABLES
    ${_executables}
    LIBRARIES
    ${_libraries}
    RESOLVED_DEPENDENCIES_VAR
    _verified
    UNRESOLVED_DEPENDENCIES_VAR
    _unresolved
    POST_EXCLUDE_REGEXES
    "^/System/Library/"
    "^/usr/lib/")
if(_unresolved)
    message(FATAL_ERROR "Native package has unresolved dependencies: ${_unresolved}")
endif()
foreach(_dependency IN LISTS _verified)
    file(REAL_PATH "${_dependency}" _resolved)
    cmake_path(IS_PREFIX _root "${_resolved}" NORMALIZE _inside)
    if(NOT _inside)
        message(FATAL_ERROR "Native package still uses an external library: ${_resolved}")
    endif()
endforeach()
message(STATUS "Verified self-contained native macOS executable: ${_executable}")
