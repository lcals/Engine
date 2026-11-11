# 图形和计算 Shader 使用同一目标平台与后端。
function(engine_render_shader_profile platform profile)
    if(EMSCRIPTEN)
        set(_platform asm.js)
        set(_profile wgsl)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
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
            set(_shaderc_hints "${VCPKG_INSTALLED_DIR}/${VCPKG_HOST_TRIPLET}/tools/bgfx")
            if(VCPKG_INSTALLED_DIR AND NOT VCPKG_HOST_TRIPLET)
                file(GLOB _shaderc_installed_tools LIST_DIRECTORIES TRUE
                    "${VCPKG_INSTALLED_DIR}/*/tools/bgfx")
                list(APPEND _shaderc_hints ${_shaderc_installed_tools})
            endif()
            unset(_shaderc_program CACHE)
            find_program(
                _shaderc_program
                NAMES shaderc bgfx-shaderc
                HINTS ${_shaderc_hints} NO_CACHE
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
        OUTPUT_VARIABLE _shaderc_version
        ERROR_VARIABLE _shaderc_version_error
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

    # 跨编译时工具在宿主运行，但其 Shader 格式必须匹配目标 bgfx 的 API。
    # 不以 shaderc 的可执行性或显式缓存路径代替版本兼容检查。
    set(_bgfx_header_hints)
    foreach(_include IN LISTS _bgfx_includes)
        if(NOT _include MATCHES "\\$<")
            list(APPEND _bgfx_header_hints "${_include}")
        endif()
    endforeach()
    find_file(_bgfx_defines NAMES bgfx/defines.h HINTS ${_bgfx_header_hints}
              NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH NO_CACHE)
    if(NOT _bgfx_defines)
        message(FATAL_ERROR "Cannot inspect target bgfx API from its imported headers")
    endif()
    file(STRINGS "${_bgfx_defines}" _bgfx_api_line REGEX "^#define BGFX_API_VERSION ")
    string(REGEX MATCH "BGFX_API_VERSION[ \t]+UINT32_C\\(([0-9]+)\\)" _bgfx_api_match
           "${_bgfx_api_line}")
    if(NOT _bgfx_api_match)
        message(FATAL_ERROR "Cannot read target bgfx API version: ${_bgfx_defines}")
    endif()
    set(_bgfx_api "${CMAKE_MATCH_1}")
    string(REGEX MATCH "shaderc, bgfx shader compiler tool, version [0-9]+\\.[0-9]+\\.([0-9]+)\\."
           _shaderc_api_match "${_shaderc_version}${_shaderc_version_error}")
    if(NOT _shaderc_api_match OR NOT CMAKE_MATCH_1 STREQUAL _bgfx_api)
        message(FATAL_ERROR
            "Host shaderc must match target bgfx API ${_bgfx_api}: ${ENGINE_SHADERC_EXECUTABLE}\n"
            "${_shaderc_version}${_shaderc_version_error}")
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
            if(NOT EXISTS "${_source}/${_stage}_${_shader}.sc")
                continue()
            endif()
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
                        "${_source}/color_space.sh"
                        "${_source}/dfg_approx.sh"
                        "${_source}/material_helpers.sh"
                        "${_source}/shadow_helpers.sh"
                        "${_source}/csm_helpers.sh"
                        "${_source}/hex_tiling.sh"
                        "${_source}/pbr_surface.sh"
                        "${_source}/pbr_lighting.sh"
                        "${_source}/local_shadow_helpers.sh"
                        "${_source}/cluster/lighting.sh"
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
    foreach(_name vs_hdr fs_hdr_bright fs_hdr_blur fs_hdr_resolve fs_hdr_copy fs_capture_sky)
        if(_name STREQUAL "vs_hdr")
            set(_type vertex)
        else()
            set(_type fragment)
        endif()
        set(_shader_source "${_source}/${_name}.sc")
        if(_name STREQUAL "fs_capture_sky")
            set(_shader_source "${_source}/../fs_capture_sky.sc")
        endif()
        set(_header "${_output}/${_name}.bin.h")
        add_custom_command(
            OUTPUT "${_header}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${_output}"
            COMMAND
                "${ENGINE_SHADERC_EXECUTABLE}" -f "${_shader_source}" -o "${_header}"
                --type "${_type}" --platform "${_platform}" --profile "${_profile}"
                --varyingdef "${_source}/hdr_varying.def.sc"
                -i "${ENGINE_BGFX_SHADER_INCLUDE_DIR}" --bin2c "${_name}"
            DEPENDS "${_shader_source}"
                    "${_source}/hdr_helpers.sh"
                    "${_source}/../color_space.sh"
                    "${_source}/hdr_varying.def.sc"
                    "${ENGINE_BGFX_SHADER_INCLUDE_DIR}/bgfx_shader.sh"
                    "${ENGINE_SHADERC_EXECUTABLE}"
            COMMENT "Compiling embedded ${_platform}/${_profile} ${_name} shader"
            VERBATIM)
        target_sources(${target} PRIVATE "${_header}")
    endforeach()
    target_include_directories(${target} PRIVATE "${_output}")
endfunction()

# 蒙皮输出标准顶点流，全部项目材质及深度通道共用。
function(engine_embed_skinning_shader target)
    if(NOT ENGINE_SHADERC_EXECUTABLE OR NOT ENGINE_BGFX_SHADER_INCLUDE_DIR)
        message(FATAL_ERROR "Initialize render shader tools before embedding skinning shader")
    endif()
    engine_render_shader_profile(_platform _profile)
    set(_source "${PROJECT_SOURCE_DIR}/src/engine/runtime/render/shaders/cs_skinning.sc")
    set(_output "${CMAKE_CURRENT_BINARY_DIR}/generated/render")
    set(_header "${_output}/cs_skinning.bin.h")
    add_custom_command(
        OUTPUT "${_header}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_output}"
        COMMAND "${ENGINE_SHADERC_EXECUTABLE}" -f "${_source}" -o "${_header}"
                --type compute --platform "${_platform}" --profile "${_profile}"
                -i "${ENGINE_BGFX_SHADER_INCLUDE_DIR}" --bin2c cs_skinning
        DEPENDS "${_source}" "${ENGINE_BGFX_SHADER_INCLUDE_DIR}/bgfx_compute.sh"
                "${ENGINE_BGFX_SHADER_INCLUDE_DIR}/bgfx_shader.sh" "${ENGINE_SHADERC_EXECUTABLE}"
        COMMENT "Compiling embedded ${_platform}/${_profile} skinning shader"
        VERBATIM)
    target_sources(${target} PRIVATE "${_header}")
    target_include_directories(${target} PRIVATE "${_output}")
endfunction()

# Forward+ uses one compute pass; SSR shares the existing HDR full-screen vertex shader.
function(engine_embed_clustered_shader target)
    engine_render_shader_profile(_platform _profile)
    set(_source "${PROJECT_SOURCE_DIR}/src/engine/runtime/render/shaders/cluster")
    set(_output "${CMAKE_CURRENT_BINARY_DIR}/generated/render")
    set(_header "${_output}/cs_cluster_cull.bin.h")
    add_custom_command(
        OUTPUT "${_header}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_output}"
        COMMAND "${ENGINE_SHADERC_EXECUTABLE}" -f "${_source}/cs_cluster_cull.sc"
                -o "${_header}" --type compute --platform "${_platform}" --profile "${_profile}"
                -i "${ENGINE_BGFX_SHADER_INCLUDE_DIR}" --bin2c cs_cluster_cull
        DEPENDS "${_source}/cs_cluster_cull.sc" "${_source}/lighting.sh"
                "${ENGINE_BGFX_SHADER_INCLUDE_DIR}/bgfx_compute.sh" "${ENGINE_SHADERC_EXECUTABLE}"
        COMMENT "Compiling embedded ${_platform}/${_profile} clustered lighting shader"
        VERBATIM)
    target_sources(${target} PRIVATE "${_header}")
endfunction()

function(engine_embed_ssr_shaders target)
    engine_render_shader_profile(_platform _profile)
    set(_source "${PROJECT_SOURCE_DIR}/src/engine/runtime/render/shaders")
    set(_output "${CMAKE_CURRENT_BINARY_DIR}/generated/render")
    foreach(_name fs_ssr_reduce fs_ssr_trace fs_ssr_composite)
        set(_header "${_output}/${_name}.bin.h")
        add_custom_command(
            OUTPUT "${_header}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${_output}"
            COMMAND "${ENGINE_SHADERC_EXECUTABLE}" -f "${_source}/ssr/${_name}.sc"
                    -o "${_header}" --type fragment --platform "${_platform}" --profile "${_profile}"
                    --varyingdef "${_source}/hdr/hdr_varying.def.sc"
                    -i "${ENGINE_BGFX_SHADER_INCLUDE_DIR}" -i "${_source}" --bin2c "${_name}"
            DEPENDS "${_source}/ssr/${_name}.sc" "${_source}/ssr/ssr_common.sh"
                    "${_source}/dfg_approx.sh"
                    "${_source}/hdr/hdr_varying.def.sc"
                    "${ENGINE_BGFX_SHADER_INCLUDE_DIR}/bgfx_shader.sh" "${ENGINE_SHADERC_EXECUTABLE}"
            COMMENT "Compiling embedded ${_platform}/${_profile} ${_name} shader"
            VERBATIM)
        target_sources(${target} PRIVATE "${_header}")
    endforeach()
endfunction()

# Fixed upstream atmosphere and scene-only wireframe shaders, compiled for this backend.
function(engine_embed_upstream_feature_shaders target)
    engine_render_shader_profile(_platform _profile)
    set(_output "${CMAKE_CURRENT_BINARY_DIR}/generated/render")
    foreach(_entry IN ITEMS atmosphere/cs_atmo_transmittance atmosphere/cs_atmo_multiscatter
            atmosphere/cs_atmo_skyview atmosphere/vs_atmosphere atmosphere/fs_atmosphere
            debug/vs_debug_wireframe debug/fs_debug_wireframe)
        get_filename_component(_name "${_entry}" NAME)
        get_filename_component(_directory "${_entry}" DIRECTORY)
        set(_source "${PROJECT_SOURCE_DIR}/src/engine/runtime/render/shaders/${_directory}")
        string(SUBSTRING "${_name}" 0 2 _prefix)
        if(_prefix STREQUAL "cs")
            set(_type compute)
        elseif(_prefix STREQUAL "vs")
            set(_type vertex)
        else()
            set(_type fragment)
        endif()
        set(_varying "${_source}/varying.def.sc")
        if(_directory STREQUAL "debug")
            set(_varying "${_source}/debug_varying.def.sc")
        endif()
        set(_args)
        if(NOT _type STREQUAL "compute")
            list(APPEND _args --varyingdef "${_varying}")
        endif()
        file(GLOB _includes CONFIGURE_DEPENDS "${_source}/*.sh" "${_source}/*.sc")
        set(_header "${_output}/${_name}.bin.h")
        add_custom_command(OUTPUT "${_header}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${_output}"
            COMMAND "${ENGINE_SHADERC_EXECUTABLE}" -f "${_source}/${_name}.sc" -o "${_header}"
                --type "${_type}" --platform "${_platform}" --profile "${_profile}"
                -i "${ENGINE_BGFX_SHADER_INCLUDE_DIR};${_source}" --bin2c "${_name}" ${_args}
            DEPENDS ${_includes} "${ENGINE_SHADERC_EXECUTABLE}"
                "${ENGINE_BGFX_SHADER_INCLUDE_DIR}/bgfx_shader.sh"
            VERBATIM)
        target_sources(${target} PRIVATE "${_header}")
    endforeach()
endfunction()
