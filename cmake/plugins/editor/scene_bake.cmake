function(engine_configure_scene_bake_plugin)
    engine_add_system(plugins/editor scene_bake)
    target_sources(engine_scene_bake PRIVATE
        src/engine/plugins/editor/scene_bake/ibl_bake_job.cpp
        src/engine/plugins/editor/scene_bake/scene_bake_files.cpp)
    target_include_directories(engine_scene_bake PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    get_target_property(_toml_includes tomlplusplus::tomlplusplus INTERFACE_INCLUDE_DIRECTORIES)
    target_include_directories(engine_scene_bake SYSTEM PRIVATE ${_toml_includes})
    target_compile_definitions(engine_scene_bake PRIVATE TOML_HEADER_ONLY=1 TOML_EXCEPTIONS=0
                                                       JSON_NOEXCEPTION=1)
    target_link_libraries(engine_scene_bake PRIVATE engine::editor engine::asset_pipeline
        engine::render engine::resource bgfx::bgfx bgfx::bimg bgfx::bimg_encode bgfx::bx
        basisu::basisu_encoder)
    if(APPLE)
        set(_platform osx)
        set(_profile metal)
    elseif(WIN32)
        set(_platform windows)
        set(_profile s_5_0)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(_platform linux)
        set(_profile spirv)
    else()
        message(FATAL_ERROR "Scene baking requires a desktop editor platform")
    endif()
    set(_source "${PROJECT_SOURCE_DIR}/src/engine/plugins/editor/scene_bake/shaders")
    set(_output "${CMAKE_CURRENT_BINARY_DIR}/generated/scene_bake")
    foreach(_shader panorama irradiance prefilter lut)
        set(_header "${_output}/cs_bake_${_shader}.bin.h")
        add_custom_command(OUTPUT "${_header}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${_output}"
            COMMAND "${ENGINE_SHADERC_EXECUTABLE}" -f "${_source}/cs_bake_${_shader}.sc"
                -o "${_header}" --type compute --platform "${_platform}" --profile "${_profile}"
                -i "${ENGINE_BGFX_SHADER_INCLUDE_DIR}" --bin2c "cs_bake_${_shader}"
            DEPENDS "${_source}/cs_bake_${_shader}.sc" "${_source}/ibl_common.sh"
                "${ENGINE_BGFX_SHADER_INCLUDE_DIR}/bgfx_compute.sh" "${ENGINE_SHADERC_EXECUTABLE}"
            VERBATIM)
        target_sources(engine_scene_bake PRIVATE "${_header}")
    endforeach()
    target_include_directories(engine_scene_bake PRIVATE "${_output}")
    if("editor/mcp" IN_LIST ENGINE_SELECTED_PLUGINS)
        add_library(engine_scene_bake_mcp_tools STATIC
            src/engine/plugins/editor/scene_bake/scene_bake_mcp_tools.cpp)
        add_library(engine::scene_bake_mcp_tools ALIAS engine_scene_bake_mcp_tools)
        target_link_libraries(engine_scene_bake_mcp_tools PUBLIC engine::project_mcp_tools)
        engine_apply_options(engine_scene_bake_mcp_tools)
    endif()
endfunction()

engine_register_plugin(NAME editor/scene_bake CONFIGURE engine_configure_scene_bake_plugin
    DEPENDS runtime/resource_graph OPTIONS ENGINE_ENABLE_RUNTIME_DEPS ENGINE_ENABLE_EDITOR_DEPS
        ENGINE_ENABLE_ASSET_PIPELINE_DEPS)
