function(engine_configure_modeling_plugin)
    add_library(
        engine_modeling STATIC
        src/engine/plugins/editor/modeling/editable_mesh.cpp
        src/engine/plugins/editor/modeling/mesh_topology.cpp
        src/engine/plugins/editor/modeling/mesh_primitives.cpp
        src/engine/plugins/editor/modeling/mesh_geometry.cpp
        src/engine/plugins/editor/modeling/mesh_geometry_uv.cpp
        src/engine/plugins/editor/modeling/mesh_geometry_atlas.cpp
        src/engine/plugins/editor/modeling/mesh_geometry_solid.cpp)
    add_library(engine::modeling ALIAS engine_modeling)
    target_include_directories(engine_modeling PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    target_link_libraries(
        engine_modeling
        PUBLIC engine::core engine::scene_data
        PRIVATE engine::resource earcut_hpp::earcut_hpp manifold::manifold xatlas::xatlas)
    engine_apply_options(engine_modeling)
    if(TARGET engine_editor)
        add_library(engine_modeling_editor STATIC
                    src/engine/plugins/editor/modeling/modeling_commands.cpp)
        add_library(engine::modeling_editor ALIAS engine_modeling_editor)
        target_include_directories(engine_modeling_editor
                                   PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
        get_target_property(_modeling_toml_includes tomlplusplus::tomlplusplus
                            INTERFACE_INCLUDE_DIRECTORIES)
        target_include_directories(engine_modeling_editor SYSTEM PRIVATE ${_modeling_toml_includes})
        target_link_libraries(
            engine_modeling_editor
            PUBLIC engine::core
            PRIVATE engine::modeling engine::editor engine::world engine::resource SDL3::SDL3)
        target_compile_definitions(engine_modeling_editor
                                   PRIVATE TOML_HEADER_ONLY=1 TOML_EXCEPTIONS=0 JSON_NOEXCEPTION=1)
        engine_apply_options(engine_modeling_editor)
    endif()
    if(TARGET engine_asset_pipeline)
        target_link_libraries(engine_asset_pipeline PRIVATE engine::modeling)
        target_compile_definitions(engine_asset_pipeline PRIVATE ENGINE_HAS_MODELING_PLUGIN=1)
    endif()
    if("editor/mcp" IN_LIST ENGINE_SELECTED_PLUGINS)
        add_library(engine_modeling_mcp_tools STATIC
                    src/engine/plugins/editor/modeling/modeling_mcp_tools.cpp)
        add_library(engine::modeling_mcp_tools ALIAS engine_modeling_mcp_tools)
        target_link_libraries(engine_modeling_mcp_tools PUBLIC engine::project_mcp_tools)
        engine_apply_options(engine_modeling_mcp_tools)
    endif()
endfunction()

engine_register_plugin(
    NAME
    editor/modeling
    CONFIGURE
    engine_configure_modeling_plugin
    OPTIONS
    ENGINE_ENABLE_RUNTIME_DEPS
    VCPKG_FEATURES
    modeling)
