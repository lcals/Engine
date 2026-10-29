#pragma once

#include "runtime/core/guid_map.hpp"
#include "runtime/schema/asset_catalog_generated.h"
#include "runtime/schema/material_generated.h"
#include "runtime/schema/mesh_generated.h"
#include "runtime/schema/scene_generated.h"
#include "runtime/schema/shader_generated.h"
#include "runtime/system/system.hpp"

#include <cstddef>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace engine
{

struct ResourceCatalog
{
    GuidMap identities;
    schema::AssetCatalogDataT data;
    std::map<Guid, std::vector<std::byte>, std::less<>> artifacts;

    [[nodiscard]] const schema::AssetEntryT* find(std::string_view guid) const noexcept;
    [[nodiscard]] bool read_binary(std::string_view guid, std::vector<std::byte>& output) const;
    [[nodiscard]] bool read_text(std::string_view guid, std::string& output) const;
};

class ResourceSystem final : public System
{
public:
    ResourceSystem() noexcept;
    ~ResourceSystem() noexcept override;

    // 同步读取普通文件或 Android 的 apk:/assets/ 路径；上限 64 MiB，失败清空输出。
    // 不要求系统初始化；APK 读取要求宿主的 SDL Android 环境已就绪。
    [[nodiscard]] static bool read_binary(const std::filesystem::path& path,
                                          std::vector<std::byte>& output);
    // 保留原始字节及 NUL，不转码；空文件成功。
    [[nodiscard]] static bool read_text(const std::filesystem::path& path, std::string& output);

    // cache_root 下读取 catalog.bin 和 gen/<完整GUID>，校验全部产物后输出独立字节快照。
    [[nodiscard]] static bool read_catalog(const std::filesystem::path& cache_root,
                                           ResourceCatalog& output);
    // 调用者先释放旧场景；GPU已复制字节可延后退役。候选预检不得调用。
    // 仅激活当前目标平台，失败保持原定义与活动目录。
    [[nodiscard]] static bool activate_catalog(const ResourceCatalog& catalog);
    [[nodiscard]] static bool path_to_guid(std::string_view path, Guid& output);
    [[nodiscard]] static bool guid_to_path(std::string_view guid, std::string& output);
    [[nodiscard]] static bool read_scene(std::string_view path, schema::SceneDataT& output);
    [[nodiscard]] static bool register_mesh(const schema::MeshDataT& data);
    [[nodiscard]] static const schema::MeshDataT* find_mesh(std::string_view path) noexcept;

    // 注册表为单宿主、主线程资源；成功时深拷贝。仅接受 builtin/ 与 project/ 路径。
    // 同一完整路径在 Shader 和材质之间唯一；内置材质不能引用项目 Shader。
    [[nodiscard]] static bool register_shader(const schema::ShaderDataT& data);
    [[nodiscard]] static bool register_material(const schema::MaterialDataT& data);
    // 返回只读借用，不得修改或转移其嵌套数据；注册不替换已有资源。
    // 全部清理使所有借用失效，项目清理仅使 project/ 借用失效。
    [[nodiscard]] static const schema::ShaderDataT* find_shader(std::string_view path) noexcept;
    [[nodiscard]] static const schema::MaterialDataT* find_material(std::string_view path) noexcept;
    // 所有借用定义的 CPU 场景必须先释放；GPU已复制资源字节，可独立退役。
    static void clear_project_definitions() noexcept;
    static void clear_definitions() noexcept;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    void on_shutdown() noexcept override;
};

} // namespace engine
