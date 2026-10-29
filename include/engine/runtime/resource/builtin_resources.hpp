#pragma once

#include <string_view>

namespace engine
{

namespace schema
{
struct AssetCatalogDataT;
struct AssetEntryT;
} // namespace schema

// 当前引擎编入的固定身份、类型与必要依赖；无需窗口、GPU 或系统初始化。
// 元数据没有物理位置，实际数据由运行平台编入的内置定义提供。
[[nodiscard]] const schema::AssetCatalogDataT& builtin_resource_catalog();
[[nodiscard]] const schema::AssetEntryT* builtin_resource_info(std::string_view guid);

// 注册引擎内置的 Mesh、Shader 与材质定义。
[[nodiscard]] bool register_builtin_render_resources();

} // namespace engine
