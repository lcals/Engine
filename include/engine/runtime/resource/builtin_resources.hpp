#pragma once

#include "runtime/resource/virtual_file_system.hpp"

#include <string_view>

namespace engine
{

namespace schema
{
struct AssetCatalogDataT;
struct AssetEntryT;
} // namespace schema

// 当前引擎编入的固定身份、类型与必要依赖；无需窗口、GPU 或系统初始化。
// 元数据没有物理位置；运行包为其提供实体，无清单启动使用编入的备用定义。
[[nodiscard]] const schema::AssetCatalogDataT& builtin_resource_catalog();
[[nodiscard]] const schema::AssetEntryT* builtin_resource_info(std::string_view guid);

// 序列化固定定义为对应 FlatBuffers 载荷；不访问活动注册表，不进行文件或 GPU 操作。
// Shader 初始字节码属于当前宿主；异平台构建必须由编辑器替换成目标平台字节码。
[[nodiscard]] bool builtin_resource_payload(std::string_view guid, ByteBuffer& output);

// 注册引擎内置的 Mesh、Shader 与材质定义。
[[nodiscard]] bool register_builtin_render_resources();

} // namespace engine
