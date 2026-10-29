cmake_minimum_required(VERSION 3.25)

foreach(_variable IN ITEMS ENGINE_SHADERC ENGINE_TEXTUREC ENGINE_GEOMETRYC ENGINE_OUTPUT_DIR)
    if(NOT DEFINED ${_variable} OR "${${_variable}}" STREQUAL "")
        message(FATAL_ERROR "Missing ${_variable}")
    endif()
endforeach()

set(_fixtures "${CMAKE_CURRENT_LIST_DIR}/fixtures/asset_tools")
file(MAKE_DIRECTORY "${ENGINE_OUTPUT_DIR}")
file(COPY_FILE "${_fixtures}/triangle.obj.in" "${ENGINE_OUTPUT_DIR}/triangle.obj")

function(run_tool _tool _output _magic)
    # 先删掉旧产物，防止失败的工具留下前次成功结果。
    file(REMOVE "${_output}")
    execute_process(
        COMMAND "${_tool}" ${ARGN}
        RESULT_VARIABLE _status
        OUTPUT_VARIABLE _stdout
        ERROR_VARIABLE _stderr
        TIMEOUT 30)
    if(NOT "${_status}" STREQUAL "0")
        message(FATAL_ERROR "${_tool} failed (${_status})\n${_stdout}\n${_stderr}")
    endif()
    if(NOT EXISTS "${_output}")
        message(FATAL_ERROR "${_tool} did not create ${_output}")
    endif()
    file(SIZE "${_output}" _size)
    string(LENGTH "${_magic}" _magic_length)
    math(EXPR _magic_bytes "${_magic_length} / 2")
    if(_size LESS_EQUAL _magic_bytes)
        message(FATAL_ERROR "${_output} contains no payload")
    endif()
    file(
        READ "${_output}" _header
        LIMIT ${_magic_bytes}
        HEX)
    if(NOT "${_header}" STREQUAL "${_magic}")
        message(FATAL_ERROR "Unexpected header in ${_output}: ${_header}")
    endif()
endfunction()

if(CMAKE_HOST_APPLE)
    set(_shader_platform osx)
elseif(CMAKE_HOST_WIN32)
    set(_shader_platform windows)
else()
    set(_shader_platform linux)
endif()

set(_shader "${ENGINE_OUTPUT_DIR}/triangle_shader.bin")
run_tool(
    "${ENGINE_SHADERC}"
    "${_shader}"
    "565348"
    -f
    "${_fixtures}/vs_triangle.sc"
    -o
    "${_shader}"
    --type
    vertex
    --platform
    "${_shader_platform}"
    --profile
    120
    --varyingdef
    "${_fixtures}/varying.def.sc"
    --Werror)

set(_texture "${ENGINE_OUTPUT_DIR}/blocks.ktx")
run_tool(
    "${ENGINE_TEXTUREC}"
    "${_texture}"
    "ab4b5458203131bb0d0a1a0a"
    -f
    "${_fixtures}/blocks.png"
    -o
    "${_texture}"
    -t
    RGBA8
    --linear
    --validate)
file(
    READ "${_texture}" _dimensions
    OFFSET 36
    LIMIT 12
    HEX)
if(NOT "${_dimensions}" STREQUAL "080000000800000000000000")
    message(FATAL_ERROR "Texture dimensions must remain 8 x 8: ${_dimensions}")
endif()

set(_mesh "${ENGINE_OUTPUT_DIR}/triangle_mesh.bin")
run_tool(
    "${ENGINE_GEOMETRYC}"
    "${_mesh}"
    "5642"
    -f
    "${ENGINE_OUTPUT_DIR}/triangle.obj"
    -o
    "${_mesh}"
    --ccw
    --tangent)

message(STATUS "Compiled and checked shader, texture, and mesh outputs")
