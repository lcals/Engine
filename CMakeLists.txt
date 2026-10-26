cmake_minimum_required(VERSION 3.25)

project(cpp_template VERSION 0.1.0 DESCRIPTION "Reusable C++20 project template" LANGUAGES CXX)

if(PROJECT_IS_TOP_LEVEL AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build configuration for single-config generators" FORCE)
endif()

option(CPP_TEMPLATE_BUILD_APP "Build the example command-line application" ${PROJECT_IS_TOP_LEVEL})
option(CPP_TEMPLATE_BUILD_TESTS "Build this project's tests" ${PROJECT_IS_TOP_LEVEL})
option(CPP_TEMPLATE_WARNINGS_AS_ERRORS "Treat warnings in this project's targets as errors" OFF)

include(cmake/project_options.cmake)

add_subdirectory(third_party)

add_library(cpp_template_core STATIC src/greeting.cpp)
add_library(cpp_template::core ALIAS cpp_template_core)
target_compile_features(cpp_template_core PUBLIC cxx_std_20)
target_include_directories(cpp_template_core PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_link_libraries(cpp_template_core PRIVATE fmt::fmt)
cpp_template_apply_options(cpp_template_core)

if(CPP_TEMPLATE_BUILD_APP)
    add_executable(cpp_template_app src/main.cpp)
    target_link_libraries(cpp_template_app PRIVATE cpp_template::core)
    cpp_template_apply_options(cpp_template_app)
endif()

if(PROJECT_IS_TOP_LEVEL OR CPP_TEMPLATE_BUILD_TESTS)
    include(CTest)
endif()

if(BUILD_TESTING AND CPP_TEMPLATE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
