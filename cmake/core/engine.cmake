find_package(Threads REQUIRED)
add_library(
    engine_core STATIC src/engine/runtime/engine.cpp src/engine/runtime/core/guid_map.cpp
                       src/engine/runtime/frame/frame_limiter.cpp
                       src/engine/runtime/system/system.cpp src/engine/runtime/utils/path_utils.cpp)
add_library(engine::core ALIAS engine_core)
target_compile_features(engine_core PUBLIC cxx_std_20)
target_link_libraries(engine_core PRIVATE Threads::Threads)
target_include_directories(engine_core
                           PUBLIC "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include/engine>")
engine_apply_options(engine_core)
