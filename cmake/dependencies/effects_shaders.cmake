# efkbgfx 已提供从官方 Shader 转换出的 bgfx 源码；复用项目宿主 shaderc 和固定后端。
include_guard(GLOBAL)
function(engine_embed_effects_shaders target)
    if(NOT ENGINE_SHADERC_EXECUTABLE OR NOT ENGINE_BGFX_SHADER_INCLUDE_DIR)
        message(FATAL_ERROR "Initialize render shader tools before embedding effects shaders")
    endif()
    engine_render_shader_profile(_platform _profile)
    get_property(_source GLOBAL PROPERTY ENGINE_EFKBGFX_SHADER_SOURCE)
    set(_output "${CMAKE_CURRENT_BINARY_DIR}/generated/effects")
    file(MAKE_DIRECTORY "${_output}")
    set(_includes "")
    set(_entries "")
    set(_headers)
    foreach(_shape sprite model)
        foreach(_material unlit lit distortion adv_unlit adv_lit adv_distortion)
            set(_prefix "")
            set(_base "${_material}")
            if(_material MATCHES "^adv_(.+)$")
                set(_prefix "ad_")
                set(_base "${CMAKE_MATCH_1}")
                set(_varying_material Advanced)
            else()
                set(_varying_material "")
            endif()
            if(_base STREQUAL unlit)
                string(APPEND _varying_material Unlit)
            elseif(_base STREQUAL lit)
                string(APPEND _varying_material Lit)
            else()
                string(APPEND _varying_material BackDistortion)
            endif()
            foreach(_stage vs fs)
                if(_stage STREQUAL vs)
                    set(_type vertex)
                    set(_file "${_prefix}${_shape}_${_base}_vs.fx")
                    set(_varying "${_shape}_${_varying_material}_varying.def.sc")
                else()
                    set(_type fragment)
                    set(_file "${_prefix}model_${_base}_ps.fx")
                    set(_varying "model_${_varying_material}_varying.def.sc")
                endif()
                string(REPLACE "." "_" _symbol "effects_${_file}")
                set(_header "${_output}/${_file}.bin.h")
                if(NOT _header IN_LIST _headers)
                    list(APPEND _headers "${_header}")
                    add_custom_command(
                        OUTPUT "${_header}"
                        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_output}"
                        COMMAND
                            "${ENGINE_SHADERC_EXECUTABLE}" -f "${_source}/${_file}.sc" -o
                            "${_header}" --type "${_type}" --platform "${_platform}" --profile
                            "${_profile}" --varyingdef "${_source}/${_varying}" -i
                            "${ENGINE_BGFX_SHADER_INCLUDE_DIR}" --bin2c "${_symbol}"
                        DEPENDS "${_source}/${_file}.sc"
                                "${_source}/${_varying}"
                                "${_source}/defines.sh"
                                "${ENGINE_BGFX_SHADER_INCLUDE_DIR}/bgfx_shader.sh"
                                "${ENGINE_SHADERC_EXECUTABLE}"
                        COMMENT "Compiling embedded ${_platform}/${_profile} effects ${_file}"
                        VERBATIM)
                    string(APPEND _includes "#include \"${_file}.bin.h\"\n")
                endif()
                string(
                    APPEND
                    _entries
                    "    if (name == \"${_shape}_${_material}\" && stage == \"${_stage}\")\n    {\n        return ${_symbol};\n    }\n"
                )
            endforeach()
        endforeach()
    endforeach()
    set(_index "${_output}/effects_shaders.hpp")
    set(_index_content
        "// Generated from the pinned efkbgfx predefined shader set.\n#pragma once\n\n#include <cstdint>\n#include <span>\n#include <string_view>\n\n${_includes}\nnamespace engine::plugins::effects::detail\n{\ninline std::span<const std::uint8_t> effect_shader(std::string_view name, std::string_view stage)\n{\n${_entries}    return {};\n}\n}\n"
    )
    file(
        GENERATE
        OUTPUT "${_index}"
        CONTENT "${_index_content}")
    target_sources(${target} PRIVATE ${_headers} "${_index}")
    target_include_directories(${target} PRIVATE "${_output}")
endfunction()
