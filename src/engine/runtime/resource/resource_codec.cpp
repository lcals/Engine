#include "runtime/resource/resource_codec.hpp"

#include "runtime/resource/resource_catalog.hpp"

#include <algorithm>
#include <utility>

namespace engine
{

bool ResourceCodec::validate_dependencies(const ResourceCatalog&, const Resource&,
                                          std::span<const ResourcePtr>) const
{
    return true;
}

void ResourceCodec::store(Resource& output, std::unique_ptr<schema::SceneDataT> data) noexcept
{
    output.scene_ = std::move(data);
}

void ResourceCodec::store(Resource& output, std::unique_ptr<schema::MeshDataT> data) noexcept
{
    output.mesh_ = std::move(data);
}

void ResourceCodec::store(Resource& output, std::unique_ptr<schema::ShaderDataT> data) noexcept
{
    output.shader_ = std::move(data);
}

void ResourceCodec::store(Resource& output, std::unique_ptr<schema::MaterialDataT> data) noexcept
{
    output.material_ = std::move(data);
}

void ResourceCodec::store(Resource& output, std::unique_ptr<schema::TextureDataT> data) noexcept
{
    output.texture_ = std::move(data);
}

void ResourceCodec::store(Resource& output, std::unique_ptr<schema::AudioClipDataT> data) noexcept
{
    output.audio_ = std::move(data);
}

void ResourceCodec::store(Resource& output, ByteView data)
{
    output.bytes_.assign(data.begin(), data.end());
}

bool ResourceCodec::valid_path(std::string_view path) noexcept
{
    if ((!path.starts_with("builtin/") && !path.starts_with("project/")) ||
        path.find_first_of("\\:") != std::string_view::npos ||
        path.find('\0') != std::string_view::npos)
    {
        return false;
    }
    for (;;)
    {
        const auto separator = path.find('/');
        const auto part = path.substr(0, separator);
        if (part.empty() || part == "." || part == "..")
        {
            return false;
        }
        if (separator == std::string_view::npos)
        {
            return true;
        }
        path.remove_prefix(separator + 1);
    }
}

bool ResourceCodec::declares(const ResourceCatalog& catalog, const Resource& resource,
                             std::string_view guid, schema::AssetKind kind) noexcept
{
    const auto* target = catalog.find(guid);
    const auto& declared = resource.info().dependencies;
    return target != nullptr && target->kind == kind &&
           std::find(declared.begin(), declared.end(), guid) != declared.end();
}

const Resource* ResourceCodec::dependency(std::span<const ResourcePtr> resources,
                                          std::string_view guid) noexcept
{
    const auto found = std::find_if(resources.begin(), resources.end(), [guid](const auto& resource)
                                    { return resource && resource->info().guid == guid; });
    return found != resources.end() ? found->get() : nullptr;
}

} // namespace engine
