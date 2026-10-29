# 编辑多边形三角化与实体运算；运行包只使用编译后的 MeshData。
find_package(earcut_hpp CONFIG REQUIRED GLOBAL)
find_package(manifold CONFIG REQUIRED GLOBAL)
find_package(Threads REQUIRED)

# xatlas 无 vcpkg port；Git 子项目固定版本，构建仅编译已检出的源码。
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/xatlas/source/xatlas/xatlas.cpp")
    message(FATAL_ERROR "xatlas is missing; run git submodule update --init third_party/xatlas")
endif()
add_library(engine_xatlas STATIC xatlas/source/xatlas/xatlas.cpp)
add_library(xatlas::xatlas ALIAS engine_xatlas)
target_include_directories(engine_xatlas SYSTEM PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/xatlas/source/xatlas")
target_compile_features(engine_xatlas PRIVATE cxx_std_11)
target_link_libraries(engine_xatlas PRIVATE Threads::Threads)
engine_apply_compiler_cache(engine_xatlas)
