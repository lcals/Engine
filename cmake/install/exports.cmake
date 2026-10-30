include_guard(GLOBAL)

# build/ 保留编译缓存；output/ 只收集可交付程序和独立数据。
if(ANDROID
   OR IOS
   OR NOT ENGINE_BUILD_APP
   OR NOT (WIN32 OR APPLE))
    return()
endif()
if(NOT TARGET editor_app AND NOT TARGET engine_project_app)
    return()
endif()
if(WIN32
   AND (NOT VCPKG_TARGET_TRIPLET STREQUAL "x64-windows-static"
        OR NOT CMAKE_MSVC_RUNTIME_LIBRARY
        OR CMAKE_MSVC_RUNTIME_LIBRARY MATCHES "DLL"))
    return()
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
if(APPLE)
    set(_engine_export_platform macos)
    option(ENGINE_MACOS_APP_BUNDLE "Export a macOS .app instead of a direct native executable" OFF)
else()
    set(_engine_export_platform windows)
endif()
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _engine_export_arch)
if(_engine_export_arch MATCHES "^(x86_64|amd64)$")
    set(_engine_export_arch x64)
elseif(_engine_export_arch STREQUAL "aarch64")
    set(_engine_export_arch arm64)
endif()
set(_engine_export_root
    "${PROJECT_SOURCE_DIR}/output/${_engine_export_platform}/${_engine_export_arch}/$<LOWER_CASE:$<CONFIG>>"
)
set(_engine_export_arguments --platform "${_engine_export_platform}" --cmake "${CMAKE_COMMAND}")
if(APPLE AND ENGINE_MACOS_APP_BUNDLE)
    list(APPEND _engine_export_arguments --app-bundle)
endif()
if(ENGINE_V8_ICU_DATA)
    list(APPEND _engine_export_arguments --icu "${ENGINE_V8_ICU_DATA}")
endif()

set(_engine_export_targets)
if(TARGET engine_project_app)
    add_custom_target(
        engine_project_export
        COMMAND
            "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/export_desktop.py"
            ${_engine_export_arguments} --kind runtime --product "${ENGINE_EXPORT_PRODUCT}"
            --project "${ENGINE_EXPORT_PROJECT}" --manifest "${ENGINE_EXPORT_MANIFEST}" --builder
            "$<TARGET_FILE:asset_build_app>" --executable "$<TARGET_FILE:engine_project_app>"
            --output "${_engine_export_root}/runtime"
        DEPENDS engine_project_app asset_build_app
        COMMENT "Exporting project application and independent runtime resources"
        VERBATIM)
    list(APPEND _engine_export_targets engine_project_export)
endif()

if(TARGET editor_app)
    include("${CMAKE_CURRENT_LIST_DIR}/editor_tools.cmake")
    engine_editor_export_arguments(_engine_editor_export_arguments)
    set(_engine_editor_product engine-editor)
    if(APPLE AND NOT ENGINE_MACOS_APP_BUNDLE)
        set(_engine_editor_product editor_app)
    endif()
    if(TARGET engine_usd_import)
        if(APPLE AND ENGINE_MACOS_APP_BUNDLE)
            list(
                APPEND
                _engine_editor_export_arguments
                --library-dir
                "$<TARGET_FILE_DIR:usd>"
                --support-directory
                "$<TARGET_FILE_DIR:usd>/usd"
                "engine-editor.app/Contents/Frameworks/usd")
        elseif(APPLE)
            list(
                APPEND
                _engine_editor_export_arguments
                --library-dir
                "$<TARGET_FILE_DIR:usd>"
                --support-directory
                "$<TARGET_FILE_DIR:usd>/usd"
                "lib/usd")
        else()
            list(APPEND _engine_editor_export_arguments --support-directory
                 "$<TARGET_FILE_DIR:usd>/usd" "usd")
        endif()
    endif()
    add_custom_target(
        engine_editor_export
        COMMAND
            "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/export_desktop.py"
            ${_engine_export_arguments} ${_engine_editor_export_arguments} --kind editor --product
            "${_engine_editor_product}" --executable "$<TARGET_FILE:editor_app>" --output
            "${_engine_export_root}/editor"
        DEPENDS editor_app
        COMMENT "Exporting desktop editor application"
        VERBATIM)
    list(APPEND _engine_export_targets engine_editor_export)
endif()
add_custom_target(engine_export DEPENDS ${_engine_export_targets})
