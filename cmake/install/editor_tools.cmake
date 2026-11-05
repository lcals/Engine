include_guard(GLOBAL)

# 仅部署编辑器离线导入工具。运行包不需要 Node、Shader 编译器或 Schema 源文件。
function(engine_editor_export_arguments output)
    set(_arguments)
    if(NOT TARGET engine_asset_pipeline)
        set(${output}
            ""
            PARENT_SCOPE)
        return()
    endif()
    if(APPLE AND ENGINE_MACOS_APP_BUNDLE)
        set(_executables "engine-editor.app/Contents/Helpers")
        set(_data "engine-editor.app/Contents/Resources/tools")
    else()
        set(_executables "tools")
        set(_data "tools")
    endif()

    get_filename_component(_shaderc_suffix "${ENGINE_SHADERC_EXECUTABLE}" LAST_EXT)
    get_filename_component(_node_suffix "${ENGINE_NODE_EXECUTABLE}" LAST_EXT)
    set(_shaderc_real "${ENGINE_SHADERC_EXECUTABLE}")
    set(_node_real "${ENGINE_NODE_EXECUTABLE}")
    if(EXISTS "${_shaderc_real}")
        file(REAL_PATH "${_shaderc_real}" _shaderc_real)
    endif()
    if(EXISTS "${_node_real}")
        file(REAL_PATH "${_node_real}" _node_real)
    endif()
    set(ENGINE_TYPESCRIPT_PACKAGE_DIR
        "${PROJECT_SOURCE_DIR}/projects/showcase/settings/typescript/node_modules/typescript"
        CACHE PATH "Installed TypeScript package deployed with the standalone editor")
    # npm 依赖由环境安装准备；普通 configure 不强制安装，仅导出时检查复制来源。
    list(
        APPEND
        _arguments
        --support-file
        "${_shaderc_real}"
        "${_executables}/shaderc${_shaderc_suffix}"
        --support-file
        "${_node_real}"
        "${_executables}/node${_node_suffix}"
        --support-file
        "${PROJECT_SOURCE_DIR}/scripts/build_scripts.mjs"
        "${_data}/scripts/build_scripts.mjs"
        --support-file
        "${PROJECT_SOURCE_DIR}/include/engine.d.ts"
        "${_data}/include/engine.d.ts"
        --support-directory
        "${PROJECT_SOURCE_DIR}/include/schemas/runtime"
        "${_data}/include/schemas/runtime"
        --support-directory
        "${ENGINE_BGFX_SHADER_INCLUDE_DIR}"
        "${_data}/bgfx"
        --support-directory
        "${PROJECT_SOURCE_DIR}/src/engine/runtime/render/shaders"
        "${_data}/builtin-shaders"
        --support-directory
        "${ENGINE_TYPESCRIPT_PACKAGE_DIR}"
        "${_data}/node_modules/typescript")
    if(TARGET engine_asset_import)
        foreach(_tool IN ITEMS FFMPEG FFPROBE)
            string(TOLOWER "${_tool}" _name)
            file(REAL_PATH "${ENGINE_${_tool}_EXECUTABLE}" _movie_tool)
            get_filename_component(_movie_suffix "${_movie_tool}" LAST_EXT)
            list(APPEND _arguments --support-file "${_movie_tool}"
                 "${_executables}/${_name}${_movie_suffix}")
            cmake_path(GET _movie_tool PARENT_PATH _movie_bin)
            list(APPEND _arguments --library-dir "${_movie_bin}")
            cmake_path(GET _movie_bin PARENT_PATH _movie_parent)
            foreach(_candidate IN ITEMS "${_movie_parent}/lib" "${_movie_parent}/bin")
                if(IS_DIRECTORY "${_candidate}")
                    list(APPEND _arguments --library-dir "${_candidate}")
                endif()
            endforeach()
        endforeach()
        cmake_path(GET _movie_bin PARENT_PATH _movie_tools_parent)
        cmake_path(GET _movie_tools_parent FILENAME _movie_tools_name)
        if(_movie_tools_name STREQUAL "tools")
            cmake_path(GET _movie_tools_parent PARENT_PATH _movie_host_root)
            foreach(_license IN ITEMS ffmpeg libvpx)
                if(EXISTS "${_movie_host_root}/share/${_license}/copyright")
                    list(APPEND _arguments --support-file
                         "${_movie_host_root}/share/${_license}/copyright"
                         "${_data}/licenses/${_license}.txt")
                endif()
            endforeach()
            foreach(_directory IN ITEMS "${_movie_host_root}/bin" "${_movie_host_root}/lib")
                if(IS_DIRECTORY "${_directory}")
                    list(APPEND _arguments --library-dir "${_directory}")
                endif()
            endforeach()
        endif()
    endif()
    # TypeScript 7 由同版本的官方平台包提供原生编译器。源目录由同一 Node 的平台
    # 决定，不能按 configure 时已安装的包 glob，否则后续 npm ci 的包会漏出交付。
    cmake_path(GET ENGINE_TYPESCRIPT_PACKAGE_DIR PARENT_PATH _typescript_node_modules)
    execute_process(
        COMMAND "${ENGINE_NODE_EXECUTABLE}" --print "process.platform + '-' + process.arch"
        OUTPUT_VARIABLE _typescript_platform
        OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    set(_typescript_platform_name "typescript-${_typescript_platform}")
    list(APPEND _arguments --support-directory
         "${_typescript_node_modules}/@typescript/${_typescript_platform_name}"
         "${_data}/node_modules/@typescript/${_typescript_platform_name}")
    if(APPLE)
        # Homebrew Node 可能使用 @rpath/libnode；给 BundleUtilities 原安装的库目录。
        cmake_path(GET _node_real PARENT_PATH _node_bin)
        cmake_path(GET _node_bin PARENT_PATH _node_root)
        if(IS_DIRECTORY "${_node_root}/lib")
            list(APPEND _arguments --library-dir "${_node_root}/lib")
        endif()
    endif()
    set(${output}
        "${_arguments}"
        PARENT_SCOPE)
endfunction()
