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

if("editor/modeling" IN_LIST ENGINE_SELECTED_PLUGINS)
    include("${PROJECT_SOURCE_DIR}/cmake/dependencies/modeling.cmake")
endif()

if("editor/usd_import" IN_LIST ENGINE_SELECTED_PLUGINS)
    include("${PROJECT_SOURCE_DIR}/cmake/dependencies/usd_import.cmake")
endif()

include("${PROJECT_SOURCE_DIR}/cmake/dependencies/v8.cmake")
