#pragma once

#include "runtime/resource/resource.hpp"
#include "runtime/resource/resource_catalog.hpp"
#include "runtime/resource/resource_loader.hpp"
#include "runtime/resource/virtual_file_system.hpp"
#include "runtime/schema/resource_header_generated.h"
#include "runtime/system/system.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace engine
{

class ResourceSystem final : public System, public ResourceLoader
{
public:
    ResourceSystem() noexcept;
    ~ResourceSystem() noexcept override;

    // 单宿主借用；仅在成功初始化后到关闭前有效。
    [[nodiscard]] static ResourceSystem* instance() noexcept;
    [[nodiscard]] bool update_catalog(const schema::AssetCatalogDataT& data,
                                      const MountList& mounts);
    [[nodiscard]] const ResourceCatalog& catalog() const noexcept;
    [[nodiscard]] std::uint64_t generation() const noexcept;
    // 借用的 loader 须存活到恢复 nullptr；默认本系统仅提供单项加载。
    void set_loader(ResourceLoader* loader) noexcept;
    [[nodiscard]] ResourceLoader& loader() noexcept;
    [[nodiscard]] bool read_header(std::string_view guid, schema::ResourceHeaderDataT& output);
    // 单项读取和格式校验；不递归、不缓存、不拥有依赖。
    [[nodiscard]] bool load(std::string_view guid, ResourcePtr& output) override;
    [[nodiscard]] ResourcePtr find(std::string_view guid) const noexcept override;
    // 调用方提供已加载的直接引用，验证跨实体契约；不读取或保留这些引用。
    [[nodiscard]] bool validate_dependencies(const Resource& resource,
                                             std::span<const ResourcePtr> dependencies) const;
    [[nodiscard]] const std::string& last_error() const noexcept override;

    // 文件工具无需系统初始化；APK 路径要求宿主 SDL Android 环境就绪。
    [[nodiscard]] static bool read_binary(const std::filesystem::path& path, ByteBuffer& output);
    [[nodiscard]] static bool read_text(const std::filesystem::path& path, std::string& output);
    // 只读 catalog.bin 并验证元数据，不访问实体文件。
    [[nodiscard]] static bool read_catalog(const std::filesystem::path& root,
                                           ResourceCatalog& output);
    // 单实体工具不建立对象或加载依赖，供导入复用和调用方预检候选资源。
    [[nodiscard]] static bool read_resource(const ResourceCatalog& catalog, const MountList& mounts,
                                            std::string_view guid, ByteBuffer& payload);
    [[nodiscard]] static bool decode_resource(const schema::AssetEntryT& entry, ByteView entity,
                                              ByteBuffer& payload);
    [[nodiscard]] static bool encode_resource(std::string_view guid, schema::AssetKind kind,
                                              ByteView payload, ByteBuffer& output,
                                              std::uint32_t& header_size);

    [[nodiscard]] static bool path_to_guid(std::string_view path, Guid& output);
    [[nodiscard]] static bool guid_to_path(std::string_view guid, std::string& output);
    [[nodiscard]] static bool register_texture(const schema::TextureDataT& data);
    [[nodiscard]] static const schema::TextureDataT* find_texture(std::string_view path) noexcept;
    [[nodiscard]] static bool register_mesh(const schema::MeshDataT& data);
    [[nodiscard]] static const schema::MeshDataT* find_mesh(std::string_view path) noexcept;

    // 仅注册引擎编入的 builtin/ 定义；项目资源通过目录加载，不能常驻注册。
    // 同一完整路径在 Shader 和材质之间唯一；内置材质不能引用项目 Shader。
    [[nodiscard]] static bool register_shader(const schema::ShaderDataT& data);
    [[nodiscard]] static bool register_material(const schema::MaterialDataT& data);
    // 纯查询，不触发读取；项目借用仅在实际使用者持有 ResourcePtr 时有效。
    [[nodiscard]] static const schema::ShaderDataT* find_shader(std::string_view path) noexcept;
    [[nodiscard]] static const schema::MaterialDataT* find_material(std::string_view path) noexcept;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    void on_shutdown() noexcept override;

private:
    ResourceCatalog catalog_;
    VirtualFileSystem files_;
    ResourceLoader* loader_ = nullptr;
    std::uint64_t generation_ = 0;
    std::string last_error_;
};

} // namespace engine
