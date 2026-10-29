# vcpkg 包按用途在根 manifest 中声明；Git 子项目由 .gitmodules 固定版本。
find_package(fmt CONFIG REQUIRED GLOBAL)
find_package(nlohmann_json CONFIG REQUIRED GLOBAL)

if(ENGINE_ENABLE_RUNTIME_DEPS)
    include(runtime.cmake)
endif()

if(ENGINE_ENABLE_EDITOR_DEPS)
    include(editor.cmake)
endif()

if(ENGINE_ENABLE_ASSET_PIPELINE_DEPS)
    include(asset_pipeline.cmake)
endif()

include(v8.cmake)
