# Fixed, licensed excerpt from the selected bgfx release. No example host framework.
set(_debugdraw_source "${PROJECT_SOURCE_DIR}/third_party/upstream/bgfx-debugdraw")
set(_debugdraw_stage "${PROJECT_BINARY_DIR}/third_party/source/bgfx-debugdraw")
file(GLOB _debugdraw_files CONFIGURE_DEPENDS "${_debugdraw_source}/debugdraw/*.h")
foreach(_file IN LISTS _debugdraw_files)
    get_filename_component(_name "${_file}" NAME)
    configure_file("${_file}" "${_debugdraw_stage}/debugdraw/${_name}" COPYONLY)
endforeach()
configure_file("${_debugdraw_source}/packrect.h" "${_debugdraw_stage}/packrect.h" COPYONLY)
configure_file("${_debugdraw_source}/transient-buffer-utils.h"
               "${_debugdraw_stage}/transient-buffer-utils.h" COPYONLY)
file(READ "${_debugdraw_source}/debugdraw/debugdraw.cpp" _debugdraw_content)
# Deterministic application of remove-unused-bgfx-utils.patch to a build copy.
string(REPLACE "#include \"../bgfx_utils.h\"\n" "#include \"../transient-buffer-utils.h\"\n"
       _debugdraw_content "${_debugdraw_content}")
set(_debugdraw_cpp "${_debugdraw_stage}/debugdraw/debugdraw.cpp")
if(EXISTS "${_debugdraw_cpp}")
    file(READ "${_debugdraw_cpp}" _debugdraw_previous)
else()
    set(_debugdraw_previous "")
endif()
if(NOT _debugdraw_previous STREQUAL _debugdraw_content)
    file(WRITE "${_debugdraw_cpp}" "${_debugdraw_content}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${_debugdraw_source}/debugdraw/debugdraw.cpp"
    "${_debugdraw_source}/remove-unused-bgfx-utils.patch")
add_library(engine_bgfx_debugdraw STATIC "${_debugdraw_cpp}")
target_include_directories(engine_bgfx_debugdraw SYSTEM PUBLIC "${_debugdraw_stage}")
target_link_libraries(engine_bgfx_debugdraw PUBLIC bgfx::bgfx bgfx::bx)
target_compile_features(engine_bgfx_debugdraw PRIVATE cxx_std_20)
