#pragma once

#include "runtime/resource/resource.hpp"

#include <string_view>

namespace engine
{

class ResourceCatalog;

// 无状态的单项解码器。仅校验目录及调用方给出的实体，不加载或保留依赖。
class ResourceCodec
{
public:
    virtual ~ResourceCodec() noexcept = default;
    [[nodiscard]] virtual bool decode(const ResourceCatalog& catalog, ByteView payload,
                                      Resource& output) const = 0;
    [[nodiscard]] virtual bool
    validate_dependencies(const ResourceCatalog& catalog, const Resource& resource,
                          std::span<const ResourcePtr> dependencies) const;

protected:
    static void store(Resource& output, std::unique_ptr<schema::SceneDataT> data) noexcept;
    static void store(Resource& output, std::unique_ptr<schema::MeshDataT> data) noexcept;
    static void store(Resource& output, std::unique_ptr<schema::ShaderDataT> data) noexcept;
    static void store(Resource& output, std::unique_ptr<schema::MaterialDataT> data) noexcept;
    static void store(Resource& output, std::unique_ptr<schema::TextureDataT> data) noexcept;
    static void store(Resource& output, std::unique_ptr<schema::AudioClipDataT> data) noexcept;
    static void store(Resource& output, std::unique_ptr<schema::ModelDataT> data) noexcept;
    static void store(Resource& output, std::unique_ptr<schema::SkeletonDataT> data) noexcept;
    static void store(Resource& output, std::unique_ptr<schema::AnimationDataT> data) noexcept;
    static void store(Resource& output, std::unique_ptr<schema::FontDataT> data) noexcept;
    static void store(Resource& output, std::unique_ptr<schema::UiDocumentDataT> data) noexcept;
    static void store(Resource& output, ByteView data);
    [[nodiscard]] static bool valid_path(std::string_view path) noexcept;
    [[nodiscard]] static bool declares(const ResourceCatalog& catalog, const Resource& resource,
                                       std::string_view guid, schema::AssetKind kind) noexcept;
    [[nodiscard]] static const Resource* dependency(std::span<const ResourcePtr> resources,
                                                    std::string_view guid) noexcept;
};

// 按 schema 类型查询静态登记表；未知类型返回 nullptr。
[[nodiscard]] const ResourceCodec* find_resource_codec(schema::AssetKind kind) noexcept;

} // namespace engine
