# Shader 构建工具在宿主运行，输出必须与目标平台固定图形后端一致。
function(engine_embed_render_shaders target)
    set(ENGINE_SHADERC_EXECUTABLE "" CACHE FILEPATH "Host bgfx shaderc executable")
    if(NOT ENGINE_SHADERC_EXECUTABLE)
        if(TARGET bgfx::shaderc)
            foreach(_configuration RELEASE RELWITHDEBINFO DEBUG NOCONFIG)
                get_target_property(_shaderc bgfx::shaderc IMPORTED_LOCATION_${_configuration})
                if(_shaderc AND EXISTS "${_shaderc}")
                    break()
                endif()
            endforeach()
            if(NOT _shaderc OR NOT EXISTS "${_shaderc}")
                get_target_property(_shaderc bgfx::shaderc IMPORTED_LOCATION)
            endif()
        endif()
        if(NOT _shaderc OR NOT EXISTS "${_shaderc}")
            find_program(_shaderc_program NAMES shaderc bgfx-shaderc
                HINTS "${VCPKG_INSTALLED_DIR}/${VCPKG_HOST_TRIPLET}/tools/bgfx"
                NO_CMAKE_FIND_ROOT_PATH)
            set(_shaderc "${_shaderc_program}")
        endif()
        if(_shaderc AND EXISTS "${_shaderc}")
            set(ENGINE_SHADERC_EXECUTABLE "${_shaderc}" CACHE FILEPATH
                "Host bgfx shaderc executable" FORCE)
        endif()
    endif()
    if(NOT ENGINE_SHADERC_EXECUTABLE OR NOT EXISTS "${ENGINE_SHADERC_EXECUTABLE}")
        message(FATAL_ERROR "Rendering requires host bgfx[tools]. Set ENGINE_SHADERC_EXECUTABLE to host shaderc.")
    endif()
    execute_process(COMMAND "${ENGINE_SHADERC_EXECUTABLE}" --version
        RESULT_VARIABLE _shaderc_result OUTPUT_QUIET ERROR_QUIET TIMEOUT 10)
    if(NOT _shaderc_result EQUAL 0)
        message(FATAL_ERROR "ENGINE_SHADERC_EXECUTABLE cannot run on this host: ${ENGINE_SHADERC_EXECUTABLE}")
    endif()
    # find_package 位于 third_party 子目录，其普通变量不会传回根目录；从全局目标查头文件。
    get_target_property(_bgfx_includes bgfx::bgfx INTERFACE_INCLUDE_DIRECTORIES)
    set(_shader_include_hints "${BGFX_SHADER_INCLUDE_PATH}"
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/bgfx"
        "${bgfx_DIR}/../../include/bgfx")
    foreach(_include IN LISTS _bgfx_includes)
        if(NOT _include MATCHES "\\$<")
            list(APPEND _shader_include_hints "${_include}/bgfx")
        endif()
    endforeach()
    find_path(ENGINE_BGFX_SHADER_INCLUDE_DIR NAMES bgfx_shader.sh
        HINTS ${_shader_include_hints} NO_CMAKE_FIND_ROOT_PATH)
    if(NOT ENGINE_BGFX_SHADER_INCLUDE_DIR)
        message(FATAL_ERROR "bgfx shader include directory is unavailable")
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        set(_platform ios)
        set(_profile metal)
    elseif(APPLE)
        set(_platform osx)
        set(_profile metal)
    elseif(WIN32)
        set(_platform windows)
        set(_profile s_5_0)
    elseif(ANDROID)
        set(_platform android)
        set(_profile spirv)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(_platform linux)
        set(_profile spirv)
    else()
        message(FATAL_ERROR "No embedded renderer shader profile for ${CMAKE_SYSTEM_NAME}")
    endif()

    set(_source "${PROJECT_SOURCE_DIR}/src/engine/runtime/render/shaders")
    set(_output "${CMAKE_CURRENT_BINARY_DIR}/generated/render")
    foreach(_stage vs fs)
        if(_stage STREQUAL "vs")
            set(_type vertex)
        else()
            set(_type fragment)
        endif()
        set(_header "${_output}/${_stage}_cube.bin.h")
        add_custom_command(OUTPUT "${_header}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${_output}"
            COMMAND "${ENGINE_SHADERC_EXECUTABLE}"
                -f "${_source}/${_stage}_cube.sc" -o "${_header}"
                --type "${_type}" --platform "${_platform}" --profile "${_profile}"
                --varyingdef "${_source}/varying.def.sc"
                -i "${ENGINE_BGFX_SHADER_INCLUDE_DIR}"
                --bin2c "${_stage}_cube"
            DEPENDS "${_source}/${_stage}_cube.sc" "${_source}/varying.def.sc"
                "${ENGINE_BGFX_SHADER_INCLUDE_DIR}/bgfx_shader.sh"
                "${ENGINE_SHADERC_EXECUTABLE}"
            COMMENT "Compiling embedded ${_platform}/${_profile} ${_stage} cube shader"
            VERBATIM)
        target_sources(${target} PRIVATE "${_header}")
    endforeach()
    target_include_directories(${target} PRIVATE "${_output}")
endfunction()
