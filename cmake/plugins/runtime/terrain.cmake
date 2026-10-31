function(engine_configure_terrain_plugin)
    if(NOT EXISTS "${PROJECT_SOURCE_DIR}/third_party/transvoxel/Transvoxel.cpp")
        message(FATAL_ERROR "Transvoxel is missing; run git submodule update --init third_party/transvoxel")
    endif()
    engine_add_system(plugins/runtime terrain)
    target_sources(engine_terrain PRIVATE
        src/engine/plugins/runtime/terrain/terrain_component.cpp
        src/engine/plugins/runtime/terrain/terrain_volume.cpp)
    target_include_directories(engine_terrain SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/third_party/transvoxel")
    target_link_libraries(engine_terrain PUBLIC engine::world engine::physics engine::resource
                                        PRIVATE SDL3::SDL3)
endfunction()

engine_register_plugin(NAME runtime/terrain CONFIGURE engine_configure_terrain_plugin
                       OPTIONS ENGINE_ENABLE_RUNTIME_DEPS)
