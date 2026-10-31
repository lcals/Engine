# 图形和计算 Shader 使用同一目标平台与后端。
function(engine_render_shader_profile platform profile)
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

    set(${platform} "${_platform}" PARENT_SCOPE)
    set(${profile} "${_profile}" PARENT_SCOPE)
endfunction()

# Shader 构建工具在宿主运行，输出必须与目标平台固定图形后端一致。
function(engine_embed_render_shaders target)
    set(ENGINE_SHADERC_EXECUTABLE
        ""
        CACHE FILEPATH "Host bgfx shaderc executable")
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
            find_program(
                _shaderc_program
                NAMES shaderc bgfx-shaderc
                HINTS "${VCPKG_INSTALLED_DIR}/${VCPKG_HOST_TRIPLET}/tools/bgfx"
                NO_CMAKE_FIND_ROOT_PATH)
            set(_shaderc "${_shaderc_program}")
        endif()
        if(_shaderc AND EXISTS "${_shaderc}")
            set(ENGINE_SHADERC_EXECUTABLE
                "${_shaderc}"
                CACHE FILEPATH "Host bgfx shaderc executable" FORCE)
        endif()
    endif()
    if(NOT ENGINE_SHADERC_EXECUTABLE OR NOT EXISTS "${ENGINE_SHADERC_EXECUTABLE}")
        message(
            FATAL_ERROR
                "Rendering requires host bgfx[tools]. Set ENGINE_SHADERC_EXECUTABLE to host shaderc."
        )
    endif()
    execute_process(
        COMMAND "${ENGINE_SHADERC_EXECUTABLE}" --version
        RESULT_VARIABLE _shaderc_result
        OUTPUT_QUIET ERROR_QUIET
        TIMEOUT 10)
    if(NOT _shaderc_result EQUAL 0)
        message(
            FATAL_ERROR
                "ENGINE_SHADERC_EXECUTABLE cannot run on this host: ${ENGINE_SHADERC_EXECUTABLE}")
    endif()
    # find_package 位于 third_party 子目录，其普通变量不会传回根目录；从全局目标查头文件。
    get_target_property(_bgfx_includes bgfx::bgfx INTERFACE_INCLUDE_DIRECTORIES)
    set(_shader_include_hints
        "${BGFX_SHADER_INCLUDE_PATH}" "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/bgfx"
        "${bgfx_DIR}/../../include/bgfx")
    foreach(_include IN LISTS _bgfx_includes)
        if(NOT _include MATCHES "\\$<")
            list(APPEND _shader_include_hints "${_include}/bgfx")
        endif()
    endforeach()
    find_path(
        ENGINE_BGFX_SHADER_INCLUDE_DIR
        NAMES bgfx_shader.sh
        HINTS ${_shader_include_hints}
        NO_CMAKE_FIND_ROOT_PATH)
    if(NOT ENGINE_BGFX_SHADER_INCLUDE_DIR)
        message(FATAL_ERROR "bgfx shader include directory is unavailable")
    endif()

    engine_render_shader_profile(_platform _profile)

    set(_source "${PROJECT_SOURCE_DIR}/src/engine/runtime/render/shaders")
    set(_output "${CMAKE_CURRENT_BINARY_DIR}/generated/render")
    set(_shaders ${ARGN})
    if(NOT _shaders)
        set(_shaders cube)
    endif()
    foreach(_shader IN LISTS _shaders)
        set(_varying "${_source}/varying.def.sc")
        if(EXISTS "${_source}/${_shader}_varying.def.sc")
            set(_varying "${_source}/${_shader}_varying.def.sc")
        endif()
        foreach(_stage vs fs)
            if(_stage STREQUAL "vs")
                set(_type vertex)
            else()
                set(_type fragment)
            endif()
            set(_header "${_output}/${_stage}_${_shader}.bin.h")
            add_custom_command(
                OUTPUT "${_header}"
                COMMAND "${CMAKE_COMMAND}" -E make_directory "${_output}"
                COMMAND
                    "${ENGINE_SHADERC_EXECUTABLE}" -f "${_source}/${_stage}_${_shader}.sc" -o
                    "${_header}" --type "${_type}" --platform "${_platform}" --profile "${_profile}"
                    --varyingdef "${_varying}" -i "${ENGINE_BGFX_SHADER_INCLUDE_DIR}"
                    --bin2c "${_stage}_${_shader}"
                DEPENDS "${_source}/${_stage}_${_shader}.sc"
                        "${_varying}"
                        "${_source}/pbr_helpers.sh"
                        "${_source}/material_helpers.sh"
                        "${_source}/shadow_helpers.sh"
                        "${_source}/csm_helpers.sh"
                        "${ENGINE_BGFX_SHADER_INCLUDE_DIR}/bgfx_shader.sh"
                        "${ENGINE_SHADERC_EXECUTABLE}"
                COMMENT "Compiling embedded ${_platform}/${_profile} ${_stage} ${_shader} shader"
                VERBATIM)
            target_sources(${target} PRIVATE "${_header}")
        endforeach()
    endforeach()
    target_include_directories(${target} PRIVATE "${_output}")
endfunction()

# ASSAO 固定使用上游 Q2 和两次 smart blur；不编译未启用的质量分支。
function(engine_embed_assao_shaders target)
    if(NOT ENGINE_SHADERC_EXECUTABLE OR NOT ENGINE_BGFX_SHADER_INCLUDE_DIR)
        message(FATAL_ERROR "Initialize render shader tools before embedding ASSAO shaders")
    endif()
    engine_render_shader_profile(_platform _profile)
    set(_source "${PROJECT_SOURCE_DIR}/src/engine/runtime/render/shaders/assao")
    set(_output "${CMAKE_CURRENT_BINARY_DIR}/generated/render")
    foreach(_shader prepare_depths_and_normals prepare_depth_mip generate_q2 smart_blur apply)
        set(_name "cs_assao_${_shader}")
        set(_header "${_output}/${_name}.bin.h")
        add_custom_command(
            OUTPUT "${_header}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${_output}"
            COMMAND
                "${ENGINE_SHADERC_EXECUTABLE}" -f "${_source}/${_name}.sc" -o "${_header}"
                --type compute --platform "${_platform}" --profile "${_profile}"
                -i "${ENGINE_BGFX_SHADER_INCLUDE_DIR}" --bin2c "${_name}"
            DEPENDS "${_source}/${_name}.sc"
                    "${_source}/uniforms.sh"
                    "${_source}/cs_assao_generate_q.sh"
                    "${ENGINE_BGFX_SHADER_INCLUDE_DIR}/bgfx_compute.sh"
                    "${ENGINE_SHADERC_EXECUTABLE}"
            COMMENT "Compiling embedded ${_platform}/${_profile} ASSAO ${_shader} shader"
            VERBATIM)
        target_sources(${target} PRIVATE "${_header}")
    endforeach()
    target_include_directories(${target} PRIVATE "${_output}")
endfunction()

# 09-hdr 的亮部与双轴过滤；同一全屏顶点程序供三个后处理程序复用。
function(engine_embed_hdr_shaders target)
    if(NOT ENGINE_SHADERC_EXECUTABLE OR NOT ENGINE_BGFX_SHADER_INCLUDE_DIR)
        message(FATAL_ERROR "Initialize render shader tools before embedding HDR shaders")
    endif()
    engine_render_shader_profile(_platform _profile)
    set(_source "${PROJECT_SOURCE_DIR}/src/engine/runtime/render/shaders/hdr")
    set(_output "${CMAKE_CURRENT_BINARY_DIR}/generated/render")
    foreach(_name vs_hdr fs_hdr_bright fs_hdr_blur fs_hdr_resolve)
        if(_name STREQUAL "vs_hdr")
            set(_type vertex)
        else()
            set(_type fragment)
        endif()
        set(_header "${_output}/${_name}.bin.h")
        add_custom_command(
            OUTPUT "${_header}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${_output}"
            COMMAND
                "${ENGINE_SHADERC_EXECUTABLE}" -f "${_source}/${_name}.sc" -o "${_header}"
                --type "${_type}" --platform "${_platform}" --profile "${_profile}"
                --varyingdef "${_source}/hdr_varying.def.sc"
                -i "${ENGINE_BGFX_SHADER_INCLUDE_DIR}" --bin2c "${_name}"
            DEPENDS "${_source}/${_name}.sc"
                    "${_source}/hdr_helpers.sh"
                    "${_source}/hdr_varying.def.sc"
                    "${ENGINE_BGFX_SHADER_INCLUDE_DIR}/bgfx_shader.sh"
                    "${ENGINE_SHADERC_EXECUTABLE}"
            COMMENT "Compiling embedded ${_platform}/${_profile} ${_name} shader"
            VERBATIM)
        target_sources(${target} PRIVATE "${_header}")
    endforeach()
    target_include_directories(${target} PRIVATE "${_output}")
endfunction()
