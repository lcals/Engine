#include "runtime/resource/resource_system.hpp"

#include "runtime/resource/builtin_resources.hpp"
#include "runtime/resource/codecs/material_resource_codec.hpp"
#include "runtime/resource/codecs/mesh_resource_codec.hpp"
#include "runtime/resource/codecs/shader_resource_codec.hpp"
#include "runtime/resource/codecs/texture_resource_codec.hpp"
#include "runtime/resource/resource_codec.hpp"

#include <SDL3/SDL_stdinc.h>
#include <flatbuffers/idl.h>
#include <flatbuffers/verifier.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <utility>

namespace engine
{
namespace
{

constexpr std::uint64_t maximum_read_size = 64U * 1024U * 1024U;
std::map<std::string, std::unique_ptr<schema::ShaderDataT>, std::less<>> shaders;
std::map<std::string, std::unique_ptr<schema::MaterialDataT>, std::less<>> materials;
std::map<std::string, std::unique_ptr<schema::MeshDataT>, std::less<>> meshes;
std::map<std::string, std::unique_ptr<schema::TextureDataT>, std::less<>> textures;
ResourceSystem* definition_owner = nullptr;

const GuidMap& builtin_identities()
{
    static const GuidMap identities = []
    {
        GuidMap result;
        for (const auto& entry : builtin_resource_catalog().entries)
        {
            if (!result.add(entry->path, entry->guid))
            {
                result.clear();
                break;
            }
        }
        return result;
    }();
    return identities;
}

std::string_view target_platform() noexcept
{
#if defined(SDL_PLATFORM_ANDROID)
    return "android";
#elif defined(SDL_PLATFORM_IOS)
    return "ios";
#elif defined(SDL_PLATFORM_MACOS)
    return "macos";
#elif defined(SDL_PLATFORM_WINDOWS)
    return "windows";
#else
    return "linux";
#endif
}

bool matches_content(ByteView bytes, const schema::AssetEntryT& entry)
{
    if (entry.content_size != bytes.size() || entry.content_hash.size() != 16)
    {
        return false;
    }
    std::uint64_t value = 14695981039346656037ULL;
    for (std::byte byte : bytes)
    {
        value ^= std::to_integer<std::uint8_t>(byte);
        value *= 1099511628211ULL;
    }
    constexpr char hex[] = "0123456789abcdef";
    std::array<char, 16> actual{};
    for (std::size_t index = actual.size(); index > 0; --index)
    {
        actual[index - 1] = hex[value & 15];
        value >>= 4;
    }
    return entry.content_hash == std::string_view(actual.data(), actual.size());
}

bool parse_header(const schema::AssetEntryT& entry, ByteView bytes,
                  schema::ResourceHeaderDataT& output)
{
    output = {};
    if (entry.header_size < 16 || entry.header_size > 65536 ||
        entry.header_size > entry.content_size || bytes.size() < entry.header_size ||
        std::memcmp(bytes.data(), "ERS1", 4) != 0)
    {
        return false;
    }
    std::uint32_t length = 0;
    for (std::size_t index = 0; index < 4; ++index)
    {
        length |= std::to_integer<std::uint32_t>(bytes[index + 4]) << (index * 8);
    }
    if (length != entry.header_size - 8)
    {
        return false;
    }
    flatbuffers::Verifier verifier(reinterpret_cast<const std::uint8_t*>(bytes.data() + 8), length);
    if (!schema::VerifyResourceHeaderDataBuffer(verifier))
    {
        return false;
    }
    auto header = schema::UnPackResourceHeaderData(bytes.data() + 8);
    if (header->guid != entry.guid || header->kind != entry.kind ||
        header->format_version != entry.format_version || header->format_version != 1 ||
        header->payload_offset != entry.header_size ||
        header->payload_size != entry.content_size - entry.header_size)
    {
        return false;
    }
    output = std::move(*header);
    return true;
}

const ResourceMount* find_mount(const MountList& mounts, std::string_view name)
{
    const auto found = std::find_if(mounts.begin(), mounts.end(),
                                    [name](const auto& mount) { return mount.name == name; });
    return found == mounts.end() ? nullptr : &*found;
}

} // namespace

ResourceSystem::ResourceSystem() noexcept = default;

ResourceSystem::~ResourceSystem() noexcept
{
    shutdown();
}

ResourceSystem* ResourceSystem::instance() noexcept
{
    return definition_owner;
}

const ResourceCatalog& ResourceSystem::catalog() const noexcept
{
    return catalog_;
}

std::uint64_t ResourceSystem::generation() const noexcept
{
    return generation_;
}

void ResourceSystem::set_loader(ResourceLoader* loader) noexcept
{
    loader_ = loader == this ? nullptr : loader;
}

ResourceLoader& ResourceSystem::loader() noexcept
{
    return loader_ != nullptr ? *loader_ : *this;
}

ResourcePtr ResourceSystem::find(std::string_view) const noexcept
{
    // 单项核心不保存已加载对象；驻留查询由可选加载器实现。
    return {};
}

const std::string& ResourceSystem::last_error() const noexcept
{
    return last_error_;
}

bool ResourceSystem::update_catalog(const schema::AssetCatalogDataT& data, const MountList& mounts)
{
    last_error_.clear();
    ResourceCatalog candidate;
    VirtualFileSystem files;
    if (generation_ == std::numeric_limits<std::uint64_t>::max() ||
        data.target_platform != target_platform() || !candidate.assign(data) ||
        !files.configure(mounts))
    {
        last_error_ = "Invalid resource catalog, platform or mounts";
        return false;
    }
    for (const auto& entry : candidate.data().entries)
    {
        if (find_mount(mounts, entry->location->mount) == nullptr)
        {
            last_error_ = "Missing resource mount: " + entry->location->mount;
            return false;
        }
    }
    catalog_ = std::move(candidate);
    files_ = std::move(files);
    ++generation_;
    return true;
}

bool ResourceSystem::read_binary(const std::filesystem::path& path, ByteBuffer& output)
{
    return VirtualFileSystem::read_file(path, output);
}

bool ResourceSystem::read_text(const std::filesystem::path& path, std::string& output)
{
    output.clear();
    ByteBuffer bytes;
    if (!read_binary(path, bytes))
    {
        return false;
    }
    if (!bytes.empty())
    {
        output.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }
    return true;
}

bool ResourceSystem::read_catalog(const std::filesystem::path& root, ResourceCatalog& output)
{
    output = {};
    ByteBuffer bytes;
    // 读取清单同样拒绝符号链接；目录实体大小不需要预先知道。
    const auto encoded = root.generic_u8string();
    if (!encoded.starts_with(u8"apk:/assets"))
    {
        std::error_code error;
        const auto directory = std::filesystem::symlink_status(root, error);
        if (error || !std::filesystem::is_directory(directory) ||
            std::filesystem::is_symlink(directory))
        {
            return false;
        }
        const auto file = std::filesystem::symlink_status(root / "catalog.bin", error);
        if (error || !std::filesystem::is_regular_file(file) || std::filesystem::is_symlink(file))
        {
            return false;
        }
    }
    if (!read_binary(root / "catalog.bin", bytes))
    {
        return false;
    }
    flatbuffers::Verifier verifier(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                   bytes.size());
    if (!schema::VerifyAssetCatalogDataBuffer(verifier) ||
        schema::GetAssetCatalogData(bytes.data())->entries()->size() > 65536)
    {
        return false;
    }
    auto data = schema::UnPackAssetCatalogData(bytes.data());
    return output.assign(*data);
}

bool ResourceSystem::encode_resource(std::string_view guid, schema::AssetKind kind,
                                     ByteView payload, ByteBuffer& output,
                                     std::uint32_t& header_size)
{
    output.clear();
    header_size = 0;
    if (!GuidMap::valid_guid(guid) || kind < schema::AssetKind::MIN ||
        kind > schema::AssetKind::MAX || payload.size() > maximum_read_size)
    {
        return false;
    }
    schema::ResourceHeaderDataT header;
    header.guid = guid;
    header.kind = kind;
    header.payload_size = payload.size();
    flatbuffers::FlatBufferBuilder builder;
    builder.ForceDefaults(true);
    builder.Finish(schema::ResourceHeaderData::Pack(builder, &header), "ERSH");
    header.payload_offset = 8 + builder.GetSize();
    builder.Clear();
    builder.ForceDefaults(true);
    builder.Finish(schema::ResourceHeaderData::Pack(builder, &header), "ERSH");
    if (8 + builder.GetSize() != header.payload_offset || header.payload_offset > 65536 ||
        payload.size() > maximum_read_size - header.payload_offset)
    {
        return false;
    }
    header_size = static_cast<std::uint32_t>(header.payload_offset);
    output.resize(header_size + payload.size());
    std::memcpy(output.data(), "ERS1", 4);
    const auto length = static_cast<std::uint32_t>(builder.GetSize());
    for (std::size_t index = 0; index < 4; ++index)
    {
        output[index + 4] = static_cast<std::byte>((length >> (index * 8)) & 255U);
    }
    std::memcpy(output.data() + 8, builder.GetBufferPointer(), builder.GetSize());
    if (!payload.empty())
    {
        std::memcpy(output.data() + header_size, payload.data(), payload.size());
    }
    return true;
}

bool ResourceSystem::decode_resource(const schema::AssetEntryT& entry, ByteView entity,
                                     ByteBuffer& payload)
{
    payload.clear();
    schema::ResourceHeaderDataT header;
    if (!matches_content(entity, entry) || !parse_header(entry, entity, header))
    {
        return false;
    }
    payload.assign(entity.begin() + entry.header_size, entity.end());
    return true;
}

bool ResourceSystem::read_resource(const ResourceCatalog& catalog, const MountList& mounts,
                                   std::string_view guid, ByteBuffer& payload)
{
    payload.clear();
    const auto* entry = catalog.find(guid);
    VirtualFileSystem files;
    ByteBuffer entity;
    return entry && entry->location && files.configure(mounts) &&
           files.read(*entry->location, 0, entry->content_size, entity) &&
           decode_resource(*entry, entity, payload);
}

bool ResourceSystem::read_header(std::string_view guid, schema::ResourceHeaderDataT& output)
{
    output = {};
    last_error_.clear();
    const auto* entry = catalog_.find(guid);
    ByteBuffer bytes;
    if (entry == nullptr || !entry->location ||
        !files_.read(*entry->location, 0, entry->header_size, bytes) ||
        !parse_header(*entry, bytes, output))
    {
        last_error_ = "Cannot read resource header: " + std::string(guid);
        return false;
    }
    return true;
}

bool ResourceSystem::load(std::string_view guid, ResourcePtr& output)
{
    // GUID 可能借用自 output 的元数据；先保存再清空调用方引用。
    const Guid identity(guid);
    guid = identity;
    output.reset();
    last_error_.clear();
    if (!is_initialized() || !GuidMap::valid_guid(guid) || catalog_.find(guid) == nullptr)
    {
        last_error_ = "Unknown resource or resource system not initialized: " + std::string(guid);
        return false;
    }
    const auto* entry = catalog_.find(guid);
    auto resource = std::shared_ptr<Resource>(new Resource());
    resource->generation_ = generation_;
    resource->info_ = *entry;
    ByteBuffer payload;
    if (!entry->path.starts_with("builtin/"))
    {
        ByteBuffer entity;
        if (!entry->location || !files_.read(*entry->location, 0, entry->content_size, entity) ||
            !decode_resource(*entry, entity, payload))
        {
            last_error_ = "Invalid resource entity: " + entry->path;
            return false;
        }
    }
    const auto* codec = find_resource_codec(entry->kind);
    if (!codec || !codec->decode(catalog_, payload, *resource))
    {
        last_error_ = "Invalid resource entity or declared reference: " + entry->path;
        return false;
    }
    output = std::move(resource);
    return true;
}

bool ResourceSystem::validate_dependencies(const Resource& resource,
                                           std::span<const ResourcePtr> dependencies) const
{
    const auto& info = resource.info();
    const auto* current = catalog_.find(info.guid);
    if (!is_initialized() || resource.generation() != generation_ || current == nullptr ||
        *current != info || dependencies.size() != info.dependencies.size())
    {
        return false;
    }
    std::map<std::string_view, const Resource*, std::less<>> references;
    for (const auto& dependency : dependencies)
    {
        if (!dependency || dependency->generation() != generation_ ||
            !references.emplace(dependency->info().guid, dependency.get()).second)
        {
            return false;
        }
        const auto* declared = catalog_.find(dependency->info().guid);
        if (declared == nullptr || *declared != dependency->info())
        {
            return false;
        }
    }
    for (const auto& guid : info.dependencies)
    {
        if (!references.contains(guid))
        {
            return false;
        }
    }
    const auto* codec = find_resource_codec(info.kind);
    return codec && codec->validate_dependencies(catalog_, resource, dependencies);
}

bool ResourceSystem::path_to_guid(std::string_view path, Guid& output)
{
    return (definition_owner &&
            definition_owner->catalog_.identities().path_to_guid(path, output)) ||
           builtin_identities().path_to_guid(path, output);
}

bool ResourceSystem::guid_to_path(std::string_view guid, std::string& output)
{
    return (definition_owner &&
            definition_owner->catalog_.identities().guid_to_path(guid, output)) ||
           builtin_identities().guid_to_path(guid, output);
}

bool ResourceSystem::register_texture(const schema::TextureDataT& data)
{
    if (!data.path.starts_with("builtin/") || !TextureResourceCodec::validate(data) ||
        textures.contains(data.path) || meshes.contains(data.path) ||
        materials.contains(data.path) || shaders.contains(data.path))
    {
        return false;
    }
    textures.emplace(data.path, std::make_unique<schema::TextureDataT>(data));
    return true;
}

const schema::TextureDataT* ResourceSystem::find_texture(std::string_view path) noexcept
{
    const auto builtin = textures.find(path);
    if (builtin != textures.end())
    {
        return builtin->second.get();
    }
    Guid guid;
    if (definition_owner && path_to_guid(path, guid))
    {
        const auto resource = definition_owner->loader().find(guid);
        return resource ? resource->texture() : nullptr;
    }
    return nullptr;
}

bool ResourceSystem::register_mesh(const schema::MeshDataT& data)
{
    if (!data.path.starts_with("builtin/") || !MeshResourceCodec::validate(data) ||
        meshes.contains(data.path) || shaders.contains(data.path) ||
        materials.contains(data.path) || textures.contains(data.path))
    {
        return false;
    }
    meshes.emplace(data.path, std::make_unique<schema::MeshDataT>(data));
    return true;
}

bool ResourceSystem::register_shader(const schema::ShaderDataT& data)
{
    if (!data.path.starts_with("builtin/") || !ShaderResourceCodec::validate(data) ||
        shaders.contains(data.path) || meshes.contains(data.path) ||
        materials.contains(data.path) || textures.contains(data.path))
    {
        return false;
    }
    shaders.emplace(data.path, std::make_unique<schema::ShaderDataT>(data));
    return true;
}

bool ResourceSystem::register_material(const schema::MaterialDataT& data)
{
    if (!data.type_path.starts_with("builtin/") ||
        !MaterialResourceCodec::validate(data, find_shader(data.shader_path)) ||
        materials.contains(data.type_path) || meshes.contains(data.type_path) ||
        shaders.contains(data.type_path) || textures.contains(data.type_path))
    {
        return false;
    }
    materials.emplace(data.type_path, std::make_unique<schema::MaterialDataT>(data));
    return true;
}

const schema::MeshDataT* ResourceSystem::find_mesh(std::string_view path) noexcept
{
    const auto builtin = meshes.find(path);
    if (builtin != meshes.end())
    {
        return builtin->second.get();
    }
    Guid guid;
    if (definition_owner && path_to_guid(path, guid))
    {
        const auto resource = definition_owner->loader().find(guid);
        return resource ? resource->mesh() : nullptr;
    }
    return nullptr;
}

const schema::ShaderDataT* ResourceSystem::find_shader(std::string_view path) noexcept
{
    const auto builtin = shaders.find(path);
    if (builtin != shaders.end())
    {
        return builtin->second.get();
    }
    Guid guid;
    if (definition_owner && path_to_guid(path, guid))
    {
        const auto resource = definition_owner->loader().find(guid);
        return resource ? resource->shader() : nullptr;
    }
    return nullptr;
}

const schema::MaterialDataT* ResourceSystem::find_material(std::string_view path) noexcept
{
    const auto builtin = materials.find(path);
    if (builtin != materials.end())
    {
        return builtin->second.get();
    }
    Guid guid;
    if (definition_owner && path_to_guid(path, guid))
    {
        const auto resource = definition_owner->loader().find(guid);
        return resource ? resource->material() : nullptr;
    }
    return nullptr;
}

bool ResourceSystem::on_initialize(const EngineConfig&)
{
    if (definition_owner != nullptr)
    {
        return false;
    }
    flatbuffers::Parser parser;
    if (!parser.Parse("table DependencyProbe { value:uint; } root_type DependencyProbe;"))
    {
        return false;
    }
    std::set<std::string> old_paths;
    for (const auto& [path, unused] : shaders)
    {
        old_paths.insert(path);
    }
    for (const auto& [path, unused] : materials)
    {
        old_paths.insert(path);
    }
    for (const auto& [path, unused] : meshes)
    {
        old_paths.insert(path);
    }
    for (const auto& [path, unused] : textures)
    {
        old_paths.insert(path);
    }
    schema::AssetCatalogDataT empty;
    empty.schema_version = 2;
    empty.revision = "builtin-only";
    empty.target_platform = target_platform();
    if (!register_builtin_render_resources() || !update_catalog(empty, {}))
    {
        std::erase_if(textures,
                      [&old_paths](const auto& item) { return !old_paths.contains(item.first); });
        std::erase_if(materials,
                      [&old_paths](const auto& item) { return !old_paths.contains(item.first); });
        std::erase_if(shaders,
                      [&old_paths](const auto& item) { return !old_paths.contains(item.first); });
        std::erase_if(meshes,
                      [&old_paths](const auto& item) { return !old_paths.contains(item.first); });
        return false;
    }
    definition_owner = this;
    return true;
}

void ResourceSystem::on_shutdown() noexcept
{
    if (definition_owner == this)
    {
        loader_ = nullptr;
        catalog_ = {};
        files_ = {};
        textures.clear();
        meshes.clear();
        materials.clear();
        shaders.clear();
        definition_owner = nullptr;
        last_error_.clear();
    }
}

} // namespace engine
