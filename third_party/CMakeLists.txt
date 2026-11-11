# vcpkg 包按用途在根 manifest 中声明；Git 子项目由 .gitmodules 固定版本。
find_package(fmt CONFIG REQUIRED GLOBAL)
find_package(nlohmann_json CONFIG REQUIRED GLOBAL)

if(ENGINE_ENABLE_RUNTIME_DEPS)
    include("${PROJECT_SOURCE_DIR}/cmake/dependencies/runtime.cmake")
endif()

if(ENGINE_ENABLE_EDITOR_DEPS)
    include("${PROJECT_SOURCE_DIR}/cmake/dependencies/editor.cmake")
endif()

if(ENGINE_ENABLE_ASSET_PIPELINE_DEPS)
    include("${PROJECT_SOURCE_DIR}/cmake/dependencies/asset_pipeline.cmake")
endif()

if(ENGINE_ENABLE_ASSET_PIPELINE_DEPS OR "editor/modeling" IN_LIST ENGINE_SELECTED_PLUGINS)
    include("${PROJECT_SOURCE_DIR}/cmake/dependencies/mesh_tangents.cmake")
endif()

if("runtime/effects" IN_LIST ENGINE_SELECTED_PLUGINS)
    include("${PROJECT_SOURCE_DIR}/cmake/dependencies/effects.cmake")
endif()

if("runtime/destruction" IN_LIST ENGINE_SELECTED_PLUGINS)
    include("${PROJECT_SOURCE_DIR}/cmake/dependencies/destruction.cmake")
endif()

if("editor/modeling" IN_LIST ENGINE_SELECTED_PLUGINS)
    include("${PROJECT_SOURCE_DIR}/cmake/dependencies/modeling.cmake")
endif()

if("editor/scene_bake" IN_LIST ENGINE_SELECTED_PLUGINS)
    include("${PROJECT_SOURCE_DIR}/cmake/dependencies/xatlas.cmake")
    include("${PROJECT_SOURCE_DIR}/cmake/dependencies/lightmapper.cmake")
endif()

if("editor/asset_import" IN_LIST ENGINE_SELECTED_PLUGINS)
    include("${PROJECT_SOURCE_DIR}/cmake/dependencies/usd_import.cmake")
endif()

if("editor/mcp" IN_LIST ENGINE_SELECTED_PLUGINS)
    include("${PROJECT_SOURCE_DIR}/cmake/dependencies/ios_device_tools.cmake")
endif()

include("${PROJECT_SOURCE_DIR}/cmake/dependencies/v8.cmake")

include("${PROJECT_SOURCE_DIR}/cmake/dependencies/script.cmake")
