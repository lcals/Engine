#include "runtime/resource/resource_system.hpp"

#include "runtime/resource/builtin_resources.hpp"
#include "runtime/utils/mesh_utils.hpp"

#include <SDL3/SDL_stdinc.h>
#include <basisu/transcoder/basisu_transcoder.h>
#include <flatbuffers/idl.h>
#include <flatbuffers/verifier.h>

#include <algorithm>
#include <array>
#include <cmath>
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

bool valid_resource_path(std::string_view path)
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

bool valid_uniform_name(std::string_view name)
{
    const auto letter = [](char value)
    { return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || value == '_'; };
    if (name.empty() || !letter(name.front()))
    {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [letter](char value)
                       { return letter(value) || (value >= '0' && value <= '9'); });
}

bool reserved_uniform(std::string_view name)
{
    constexpr std::array names = {std::string_view("u_normal_transform"),
                                  std::string_view("u_ambient"),
                                  std::string_view("u_tonemap"),
                                  std::string_view("u_shadow_transform"),
                                  std::string_view("u_shadow_settings"),
                                  std::string_view("u_shadow_depth"),
                                  std::string_view("s_shadow_map"),
                                  std::string_view("u_camera_position"),
                                  std::string_view("u_light_direction"),
                                  std::string_view("u_light_color"),
                                  std::string_view("u_light_count"),
                                  std::string_view("u_light_position_range"),
                                  std::string_view("u_light_direction_outer"),
                                  std::string_view("u_light_color_intensity"),
                                  std::string_view("u_light_kind_inner"),
                                  std::string_view("u_viewRect"),
                                  std::string_view("u_viewTexel"),
                                  std::string_view("u_view"),
                                  std::string_view("u_invView"),
                                  std::string_view("u_proj"),
                                  std::string_view("u_invProj"),
                                  std::string_view("u_viewProj"),
                                  std::string_view("u_invViewProj"),
                                  std::string_view("u_model"),
                                  std::string_view("u_modelView"),
                                  std::string_view("u_modelViewProj"),
                                  std::string_view("u_alphaRef")};
    return std::find(names.begin(), names.end(), name) != names.end();
}

bool parameter_values(const schema::MaterialValueUnion& value, std::array<double, 4>& output,
                      std::uint8_t& size)
{
    output = {};
    size = 0;
    if (value.value == nullptr)
    {
        return false;
    }
    switch (value.type)
    {
    case schema::MaterialValue::FloatValue:
        output[0] = value.AsFloatValue()->value;
        size = 1;
        break;
    case schema::MaterialValue::Vec2Value:
    {
        const auto& vector = value.AsVec2Value()->value;
        output = {vector.x(), vector.y(), 0.0, 0.0};
        size = 2;
        break;
    }
    case schema::MaterialValue::Vec3Value:
    {
        const auto& vector = value.AsVec3Value()->value;
        output = {vector.x(), vector.y(), vector.z(), 0.0};
        size = 3;
        break;
    }
    case schema::MaterialValue::Vec4Value:
    {
        const auto& vector = value.AsVec4Value()->value;
        output = {vector.x(), vector.y(), vector.z(), vector.w()};
        size = 4;
        break;
    }
    default:
        return false;
    }
    return std::all_of(output.begin(), output.begin() + size,
                       [](double item)
                       {
                           constexpr double maximum = std::numeric_limits<float>::max();
                           return std::isfinite(item) && item >= -maximum && item <= maximum;
                       });
}

bool valid_parameter_value(const schema::ShaderParameterT& definition,
                           const schema::MaterialValueUnion& value)
{
    std::array<double, 4> values{};
    std::uint8_t count = 0;
    return value.type == definition.default_value.type && parameter_values(value, values, count) &&
           (!definition.bounded ||
            std::all_of(values.begin(), values.begin() + count, [&definition](double item)
                        { return item >= definition.minimum && item <= definition.maximum; }));
}

bool valid_mesh(const schema::MeshDataT& data)
{
    return valid_resource_path(data.path) && MeshUtils::validate(data);
}

bool valid_texture(const schema::TextureDataT& data)
{
    if (data.schema_version != 1 || !valid_resource_path(data.path) || data.width == 0 ||
        data.height == 0 || data.width > 16384 || data.height > 16384 || data.mip_count == 0 ||
        data.format < schema::TextureFormat::MIN || data.format > schema::TextureFormat::MAX ||
        data.data.empty() || data.data.size() > maximum_read_size)
    {
        return false;
    }
    if (data.encoding == schema::TextureEncoding::rgba8)
    {
        return data.mip_count == 1 &&
               (data.format == schema::TextureFormat::auto_format ||
                data.format == schema::TextureFormat::rgba8) &&
               data.data.size() == static_cast<std::uint64_t>(data.width) * data.height * 4;
    }
    if (data.encoding != schema::TextureEncoding::basis)
    {
        return false;
    }
    basist::basisu_transcoder decoder;
    basist::basisu_image_info info{};
    const auto size = static_cast<std::uint32_t>(data.data.size());
    if (!decoder.validate_file_checksums(data.data.data(), size, true) ||
        decoder.get_total_images(data.data.data(), size) != 1 ||
        decoder.get_texture_type(data.data.data(), size) != basist::cBASISTexType2D ||
        !decoder.get_image_info(data.data.data(), size, info, 0))
    {
        return false;
    }
    std::uint32_t full_levels = 1;
    for (auto dimension = std::max(data.width, data.height); dimension > 1; dimension >>= 1)
    {
        ++full_levels;
    }
    if (info.m_orig_width != data.width || info.m_orig_height != data.height ||
        info.m_total_levels != data.mip_count ||
        (data.mip_count != 1 && data.mip_count != full_levels))
    {
        return false;
    }
    for (std::uint32_t level = 0; level < data.mip_count; ++level)
    {
        basist::basisu_image_level_info mip{};
        if (!decoder.get_image_level_info(data.data.data(), size, mip, 0, level) ||
            mip.m_orig_width != std::max(1U, data.width >> level) ||
            mip.m_orig_height != std::max(1U, data.height >> level))
        {
            return false;
        }
    }
    return true;
}

bool valid_shader(const schema::ShaderDataT& data)
{
    if (!valid_resource_path(data.path) || data.schema_version != 1 || data.vertex_code.empty() ||
        data.fragment_code.empty())
    {
        return false;
    }
    std::set<std::string_view> names;
    std::map<std::string_view, unsigned int> uniform_slots;
    for (const auto& parameter : data.parameters)
    {
        if (parameter == nullptr || parameter->name.empty() ||
            parameter->name.find('\0') != std::string::npos ||
            !names.insert(parameter->name).second || !valid_uniform_name(parameter->uniform_name) ||
            reserved_uniform(parameter->uniform_name) || !std::isfinite(parameter->minimum) ||
            !std::isfinite(parameter->maximum) || parameter->minimum > parameter->maximum ||
            !valid_parameter_value(*parameter, parameter->default_value))
        {
            return false;
        }
        std::array<double, 4> values{};
        std::uint8_t size = 0;
        if (!parameter_values(parameter->default_value, values, size) ||
            static_cast<unsigned int>(parameter->uniform_offset) + size > 4)
        {
            return false;
        }
        const unsigned int mask = ((1U << size) - 1U) << parameter->uniform_offset;
        unsigned int& slots = uniform_slots[parameter->uniform_name];
        if ((slots & mask) != 0)
        {
            return false;
        }
        slots |= mask;
    }
    std::set<std::uint8_t> stages;
    std::set<std::string_view> sampler_uniforms;
    for (const auto& sampler : data.samplers)
    {
        if (!sampler || sampler->name.empty() || sampler->name.find('\0') != std::string::npos ||
            !names.insert(sampler->name).second || !valid_uniform_name(sampler->uniform_name) ||
            reserved_uniform(sampler->uniform_name) ||
            uniform_slots.contains(sampler->uniform_name) ||
            !sampler_uniforms.insert(sampler->uniform_name).second || sampler->stage >= 15 ||
            !stages.insert(sampler->stage).second ||
            !sampler->default_texture.starts_with("builtin/") ||
            !valid_resource_path(sampler->default_texture))
        {
            return false;
        }
    }
    return true;
}

bool valid_material_shape(const schema::MaterialDataT& data)
{
    if (!valid_resource_path(data.type_path) || !valid_resource_path(data.shader_path) ||
        data.schema_version != 1 ||
        (data.type_path.starts_with("builtin/") && data.shader_path.starts_with("project/")))
    {
        return false;
    }
    std::set<std::string_view> names;
    for (const auto& parameter : data.parameters)
    {
        std::array<double, 4> values{};
        std::uint8_t count = 0;
        if (!parameter || parameter->name.empty() ||
            parameter->name.find('\0') != std::string::npos ||
            !names.insert(parameter->name).second ||
            !parameter_values(parameter->value, values, count) ||
            !std::all_of(values.begin(), values.begin() + count,
                         [](double value)
                         {
                             return std::isfinite(value) &&
                                    std::abs(value) <= std::numeric_limits<float>::max();
                         }))
        {
            return false;
        }
    }
    for (const auto& texture : data.textures)
    {
        if (!texture || texture->name.empty() || texture->name.find('\0') != std::string::npos ||
            !names.insert(texture->name).second || !valid_resource_path(texture->texture_path) ||
            (data.type_path.starts_with("builtin/") &&
             !texture->texture_path.starts_with("builtin/")))
        {
            return false;
        }
    }
    return true;
}

bool valid_material(const schema::MaterialDataT& data, const schema::ShaderDataT* shader)
{
    if (!valid_material_shape(data) || shader == nullptr)
    {
        return false;
    }
    for (const auto& parameter : data.parameters)
    {
        const auto definition =
            std::find_if(shader->parameters.begin(), shader->parameters.end(),
                         [&parameter](const auto& item) { return item->name == parameter->name; });
        if (definition == shader->parameters.end() || !(*definition)->exposed ||
            !valid_parameter_value(**definition, parameter->value))
        {
            return false;
        }
    }
    for (const auto& texture : data.textures)
    {
        const auto sampler = std::find_if(shader->samplers.begin(), shader->samplers.end(),
                                          [&texture](const auto& item)
                                          { return item->name == texture->name && item->exposed; });
        if (sampler == shader->samplers.end())
        {
            return false;
        }
    }
    return true;
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
    const auto reference = [this, entry](std::string_view identity, schema::AssetKind kind)
    {
        const auto* target = catalog_.find(identity);
        return target != nullptr && target->kind == kind &&
               std::find(entry->dependencies.begin(), entry->dependencies.end(), identity) !=
                   entry->dependencies.end();
    };
    bool valid = true;
    const bool builtin = entry->path.starts_with("builtin/");
    ByteBuffer payload;
    if (builtin)
    {
        switch (entry->kind)
        {
        case schema::AssetKind::texture:
        {
            const auto found = textures.find(entry->path);
            valid = found != textures.end();
            if (valid)
            {
                resource->texture_ = std::make_unique<schema::TextureDataT>(*found->second);
            }
            break;
        }
        case schema::AssetKind::mesh:
        {
            const auto found = meshes.find(entry->path);
            valid = found != meshes.end();
            if (valid)
            {
                resource->mesh_ = std::make_unique<schema::MeshDataT>(*found->second);
            }
            break;
        }
        case schema::AssetKind::shader:
        {
            const auto found = shaders.find(entry->path);
            valid = found != shaders.end();
            if (valid)
            {
                resource->shader_ = std::make_unique<schema::ShaderDataT>(*found->second);
            }
            break;
        }
        case schema::AssetKind::material:
        {
            const auto found = materials.find(entry->path);
            valid = found != materials.end();
            if (valid)
            {
                resource->material_ = std::make_unique<schema::MaterialDataT>(*found->second);
                resource->material_->shader_guid = entry->dependencies.front();
            }
            break;
        }
        default:
            valid = false;
            break;
        }
    }
    else
    {
        ByteBuffer entity;
        valid = entry->location && files_.read(*entry->location, 0, entry->content_size, entity) &&
                decode_resource(*entry, entity, payload);
        if (valid)
        {
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
            flatbuffers::Verifier verifier(bytes, payload.size());
            switch (entry->kind)
            {
            case schema::AssetKind::texture:
                valid = schema::VerifyTextureDataBuffer(verifier);
                if (valid)
                {
                    resource->texture_ = schema::UnPackTextureData(bytes);
                }
                break;
            case schema::AssetKind::scene:
                valid = schema::VerifySceneDataBuffer(verifier);
                if (valid)
                {
                    resource->scene_ = schema::UnPackSceneData(bytes);
                }
                break;
            case schema::AssetKind::mesh:
                valid = schema::VerifyMeshDataBuffer(verifier);
                if (valid)
                {
                    resource->mesh_ = schema::UnPackMeshData(bytes);
                }
                break;
            case schema::AssetKind::shader:
                valid = schema::VerifyShaderDataBuffer(verifier);
                if (valid)
                {
                    resource->shader_ = schema::UnPackShaderData(bytes);
                }
                break;
            case schema::AssetKind::material:
                valid = schema::VerifyMaterialDataBuffer(verifier);
                if (valid)
                {
                    resource->material_ = schema::UnPackMaterialData(bytes);
                }
                break;
            case schema::AssetKind::script:
            case schema::AssetKind::source:
                resource->bytes_ = std::move(payload);
                break;
            default:
                valid = false;
                break;
            }
        }
    }
    if (valid && resource->texture_)
    {
        valid = resource->texture_->path == entry->path && valid_texture(*resource->texture_);
    }
    if (valid && resource->mesh_)
    {
        valid = resource->mesh_->path == entry->path && valid_mesh(*resource->mesh_);
    }
    if (valid && resource->shader_)
    {
        valid = resource->shader_->path == entry->path && valid_shader(*resource->shader_);
        for (const auto& sampler : resource->shader_->samplers)
        {
            Guid texture_guid;
            if (!valid ||
                !catalog_.identities().path_to_guid(sampler->default_texture, texture_guid))
            {
                valid = false;
                break;
            }
            valid = reference(texture_guid, schema::AssetKind::texture);
        }
    }
    if (valid && resource->material_)
    {
        auto& material = *resource->material_;
        std::string shader_path;
        valid = reference(material.shader_guid, schema::AssetKind::shader) &&
                catalog_.identities().guid_to_path(material.shader_guid, shader_path) &&
                (material.shader_path.empty() || material.shader_path == shader_path) &&
                material.type_path == entry->path;
        if (valid)
        {
            material.shader_path = shader_path;
            valid = valid_material_shape(material);
            for (const auto& binding : material.textures)
            {
                if (!valid)
                {
                    break;
                }
                const auto* texture = catalog_.find(binding->texture_guid);
                valid = reference(binding->texture_guid, schema::AssetKind::texture) &&
                        texture != nullptr && texture->path == binding->texture_path;
            }
        }
    }
    if (valid && resource->scene_)
    {
        valid = resource->scene_->schema_version == 1 && resource->scene_->info;
        for (const auto& node : resource->scene_->nodes)
        {
            if (!node || !node->info)
            {
                valid = false;
                break;
            }
            for (const auto& component : node->components)
            {
                if (!component || component->payload.value == nullptr)
                {
                    valid = false;
                    break;
                }
                if (const auto* renderer = component->payload.AsMeshRendererData())
                {
                    valid = valid && reference(renderer->mesh_guid, schema::AssetKind::mesh) &&
                            reference(renderer->material_guid, schema::AssetKind::material);
                    for (const auto& material_guid : renderer->additional_material_guids)
                    {
                        valid = valid && reference(material_guid, schema::AssetKind::material);
                    }
                }
            }
        }
    }
    if (!valid)
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
    const auto dependency = [&references](std::string_view guid) -> const Resource*
    {
        const auto found = references.find(guid);
        return found != references.end() ? found->second : nullptr;
    };
    if (const auto* shader = resource.shader())
    {
        for (const auto& sampler : shader->samplers)
        {
            Guid guid;
            if (!catalog_.identities().path_to_guid(sampler->default_texture, guid))
            {
                return false;
            }
            const auto* texture = dependency(guid);
            if (texture == nullptr || texture->texture() == nullptr)
            {
                return false;
            }
        }
    }
    if (const auto* material = resource.material())
    {
        const auto* shader = dependency(material->shader_guid);
        if (shader == nullptr || !valid_material(*material, shader->shader()))
        {
            return false;
        }
        for (const auto& binding : material->textures)
        {
            const auto* texture = dependency(binding->texture_guid);
            if (texture == nullptr || texture->texture() == nullptr ||
                texture->info().path != binding->texture_path)
            {
                return false;
            }
        }
    }
    if (const auto* scene = resource.scene())
    {
        for (const auto& node : scene->nodes)
        {
            for (const auto& component : node->components)
            {
                if (const auto* renderer = component->payload.AsMeshRendererData())
                {
                    const auto* mesh = dependency(renderer->mesh_guid);
                    const auto* material = dependency(renderer->material_guid);
                    if (mesh == nullptr || mesh->mesh() == nullptr || material == nullptr ||
                        material->material() == nullptr)
                    {
                        return false;
                    }
                    for (const auto& guid : renderer->additional_material_guids)
                    {
                        const auto* additional = dependency(guid);
                        if (additional == nullptr || additional->material() == nullptr)
                        {
                            return false;
                        }
                    }
                }
            }
        }
    }
    return true;
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
    if (!data.path.starts_with("builtin/") || !valid_texture(data) ||
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
    if (!data.path.starts_with("builtin/") || !valid_mesh(data) || meshes.contains(data.path) ||
        shaders.contains(data.path) || materials.contains(data.path) ||
        textures.contains(data.path))
    {
        return false;
    }
    meshes.emplace(data.path, std::make_unique<schema::MeshDataT>(data));
    return true;
}

bool ResourceSystem::register_shader(const schema::ShaderDataT& data)
{
    if (!data.path.starts_with("builtin/") || !valid_shader(data) || shaders.contains(data.path) ||
        meshes.contains(data.path) || materials.contains(data.path) || textures.contains(data.path))
    {
        return false;
    }
    shaders.emplace(data.path, std::make_unique<schema::ShaderDataT>(data));
    return true;
}

bool ResourceSystem::register_material(const schema::MaterialDataT& data)
{
    if (!data.type_path.starts_with("builtin/") ||
        !valid_material(data, find_shader(data.shader_path)) ||
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
    basist::basisu_transcoder_init();
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
