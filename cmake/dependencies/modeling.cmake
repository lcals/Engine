# 编辑多边形三角化与实体运算；运行包只使用编译后的 MeshData。
find_package(earcut_hpp CONFIG REQUIRED GLOBAL)
find_package(manifold CONFIG REQUIRED GLOBAL)
include("${PROJECT_SOURCE_DIR}/cmake/dependencies/xatlas.cmake")
