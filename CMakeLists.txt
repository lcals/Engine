cmake_minimum_required(VERSION 3.25)

# 插件清单在工具链初始化前确定所需 vcpkg features。
include(cmake/plugins/selection.cmake)

project(
    engine
    VERSION 0.1.0
    DESCRIPTION "Engine C++20 project"
    LANGUAGES CXX)

include(cmake/core/options.cmake)
include(cmake/install/compiler_cache.cmake)
include(cmake/core/target_options.cmake)
include(cmake/core/systems.cmake)
add_subdirectory(third_party)

include(cmake/core/engine.cmake)
include(cmake/platforms/targets.cmake)
include(cmake/core/runtime.cmake)
include(cmake/editor/targets.cmake)
engine_configure_selected_plugins()

include(cmake/core/hosts.cmake)
include(cmake/editor/hosts.cmake)
include(cmake/install/runtime_data.cmake)

if(PROJECT_IS_TOP_LEVEL OR ENGINE_BUILD_TESTS)
    include(CTest)
endif()
if(BUILD_TESTING AND ENGINE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
