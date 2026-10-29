#include "runtime/resource/resource_system.hpp"

#include "runtime/resource/builtin_resources.hpp"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_properties.h>
#include <basisu/transcoder/basisu_transcoder.h>
#include <flatbuffers/idl.h>
#include <flatbuffers/verifier.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string_view>

namespace engine
{
namespace
{

constexpr Sint64 maximum_read_size = 64 * 1024 * 1024;

std::map<std::string, std::unique_ptr<schema::ShaderDataT>, std::less<>> shaders;
std::map<std::string, std::unique_ptr<schema::MaterialDataT>, std::less<>> materials;
std::map<std::string, std::unique_ptr<schema::MeshDataT>, std::less<>> meshes;
ResourceCatalog active_catalog;

const GuidMap& builtin_identities()
{
    static const GuidMap identities = []
    {
        GuidMap result;
        (void)result.add("builtin/shaders/pbr", "00000000-0000-4000-8000-000000000001");
        (void)result.add("builtin/shaders/unlit", "00000000-0000-4000-8000-000000000002");
        (void)result.add("builtin/materials/default-pbr", "00000000-0000-4000-8000-000000000003");
        (void)result.add("builtin/materials/default-unlit", "00000000-0000-4000-8000-000000000004");
        (void)result.add("builtin/meshes/cube", "00000000-0000-4000-8000-000000000005");
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

bool cache_file(const std::filesystem::path& root, const std::filesystem::path& relative)
{
    const auto encoded = root.generic_u8string();
    if (encoded == u8"apk:/assets" || encoded.starts_with(u8"apk:/assets/"))
    {
        return true;
    }
    std::error_code error;
    auto current = root;
    const auto root_status = std::filesystem::symlink_status(root, error);
    if (error || !std::filesystem::is_directory(root_status) ||
        std::filesystem::is_symlink(root_status))
    {
        return false;
    }
    for (const auto& part : relative)
    {
        current /= part;
        const auto status = std::filesystem::symlink_status(current, error);
        if (error || std::filesystem::is_symlink(status))
        {
            return false;
        }
    }
    return std::filesystem::is_regular_file(std::filesystem::symlink_status(current, error)) &&
           !error;
}

bool matches_content(const std::vector<std::byte>& bytes, const schema::AssetEntryT& entry)
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

const ResourceSystem* definition_owner = nullptr;

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
                                  std::string_view("u_camera_position"),
                                  std::string_view("u_light_direction"),
                                  std::string_view("u_light_color"),
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

bool apk_asset_name(std::string_view path, std::string_view& name)
{
    constexpr std::string_view prefix = "apk:/assets/";
    name = {};
    if (!path.starts_with(prefix))
    {
        return false;
    }
    const auto relative = path.substr(prefix.size());
    if (relative.empty() || relative.find_first_of("\\:") != std::string_view::npos ||
        relative.find('\0') != std::string_view::npos)
    {
        return false;
    }
    auto remainder = relative;
    for (;;)
    {
        const auto separator = remainder.find('/');
        const auto component = remainder.substr(0, separator);
        if (component.empty() || component == "." || component == "..")
        {
            return false;
        }
        if (separator == std::string_view::npos)
        {
            name = relative;
            return true;
        }
        remainder.remove_prefix(separator + 1);
    }
}

} // namespace

ResourceSystem::ResourceSystem() noexcept = default;

ResourceSystem::~ResourceSystem() noexcept
{
    shutdown();
}

bool ResourceSystem::read_binary(const std::filesystem::path& path, std::vector<std::byte>& output)
{
    output.clear();
    if (path.empty())
    {
        return false;
    }

    const auto encoded_path = path.u8string();
    if (encoded_path.find(u8'\0') != std::u8string::npos)
    {
        return false;
    }
    std::string filename(encoded_path.begin(), encoded_path.end());
    const bool apk_asset = filename.starts_with("apk:");
    if (apk_asset)
    {
        std::string_view asset_name;
        if (!apk_asset_name(filename, asset_name))
        {
            return false;
        }
#if defined(SDL_PLATFORM_ANDROID)
        filename = std::string(asset_name);
#else
        // APK 逻辑路径绝不能退化为桌面的同名磁盘文件。
        return false;
#endif
    }
    else
    {
        SDL_PathInfo info{};
        if (!SDL_GetPathInfo(filename.c_str(), &info) || info.type != SDL_PATHTYPE_FILE)
        {
            return false;
        }
    }
    SDL_IOStream* stream = SDL_IOFromFile(filename.c_str(), "rb");
    if (stream == nullptr)
    {
        return false;
    }
#if defined(SDL_PLATFORM_ANDROID)
    if (apk_asset &&
        SDL_GetPointerProperty(SDL_GetIOProperties(stream),
                               SDL_PROP_IOSTREAM_ANDROID_AASSET_POINTER, nullptr) == nullptr)
    {
        // SDL 会优先尝试私有目录同名文件；内置资源读取只接受真实 AAsset。
        (void)SDL_CloseIO(stream);
        return false;
    }
#endif

    const Sint64 file_size = SDL_GetIOSize(stream);
    bool success = file_size >= 0 && file_size <= maximum_read_size;
    if (success)
    {
        output.resize(static_cast<std::size_t>(file_size));
        std::size_t position = 0;
        while (position < output.size())
        {
            const auto count =
                SDL_ReadIO(stream, output.data() + position, output.size() - position);
            if (count == 0)
            {
                success = false;
                break;
            }
            position += count;
        }
        if (success)
        {
            // 额外探测 EOF：文件变长不能被悄悄当成完整读取，空文件同样检查 I/O 错误。
            std::byte extra{};
            success =
                SDL_ReadIO(stream, &extra, 1) == 0 && SDL_GetIOStatus(stream) == SDL_IO_STATUS_EOF;
        }
    }

    const bool closed = SDL_CloseIO(stream);
    if (!success || !closed)
    {
        output.clear();
        return false;
    }
    return true;
}

bool ResourceSystem::read_text(const std::filesystem::path& path, std::string& output)
{
    output.clear();
    std::vector<std::byte> bytes;
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

const schema::AssetEntryT* ResourceCatalog::find(std::string_view guid) const noexcept
{
    const auto item =
        std::find_if(data.entries.begin(), data.entries.end(),
                     [guid](const auto& entry) { return entry && entry->guid == guid; });
    return item == data.entries.end() ? nullptr : item->get();
}

bool ResourceCatalog::read_binary(std::string_view guid, std::vector<std::byte>& output) const
{
    output.clear();
    const auto item = artifacts.find(guid);
    if (item == artifacts.end())
    {
        return false;
    }
    output = item->second;
    return true;
}

bool ResourceCatalog::read_text(std::string_view guid, std::string& output) const
{
    output.clear();
    const auto item = artifacts.find(guid);
    if (item == artifacts.end())
    {
        return false;
    }
    if (!item->second.empty())
    {
        output.assign(reinterpret_cast<const char*>(item->second.data()), item->second.size());
    }
    return true;
}

bool ResourceSystem::read_catalog(const std::filesystem::path& cache_root, ResourceCatalog& output)
{
    output = {};
    std::vector<std::byte> bytes;
    if (!cache_file(cache_root, "catalog.bin") || !read_binary(cache_root / "catalog.bin", bytes))
    {
        return false;
    }
    flatbuffers::Verifier verifier(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                   bytes.size());
    if (!schema::VerifyAssetCatalogDataBuffer(verifier))
    {
        return false;
    }
    const auto* serialized = schema::GetAssetCatalogData(bytes.data());
    if (serialized->entries()->size() > 65536)
    {
        return false;
    }
    ResourceCatalog candidate;
    serialized->UnPackTo(&candidate.data);
    if (candidate.data.schema_version != 1 || candidate.data.revision.empty() ||
        (candidate.data.target_platform != "macos" && candidate.data.target_platform != "ios" &&
         candidate.data.target_platform != "windows" &&
         candidate.data.target_platform != "android" &&
         candidate.data.target_platform != "linux") ||
        candidate.data.entries.size() > 65536)
    {
        return false;
    }
    std::size_t total_size = 0;
    constexpr std::size_t maximum_snapshot_size = 512U * 1024U * 1024U;
    std::set<std::string> modules;
    for (const auto& entry : candidate.data.entries)
    {
        std::string builtin_path;
        if (!entry || !entry->path.starts_with("project/") ||
            builtin_identities().guid_to_path(entry->guid, builtin_path) ||
            !candidate.identities.add(entry->path, entry->guid) ||
            entry->kind < schema::AssetKind::MIN || entry->kind > schema::AssetKind::MAX)
        {
            return false;
        }
        if (entry->kind == schema::AssetKind::script &&
            (!valid_resource_path("project/" + entry->module_path) ||
             !entry->module_path.ends_with(".js") || !modules.insert(entry->module_path).second))
        {
            return false;
        }
        if (entry->content_size > maximum_snapshot_size - total_size)
        {
            return false;
        }
        total_size += static_cast<std::size_t>(entry->content_size);
        std::vector<std::byte> artifact;
        if (!cache_file(cache_root, std::filesystem::path("gen") / entry->guid) ||
            !read_binary(cache_root / "gen" / entry->guid, artifact) ||
            !matches_content(artifact, *entry))
        {
            return false;
        }
        candidate.artifacts.emplace(entry->guid, std::move(artifact));
    }
    for (const auto& entry : candidate.data.entries)
    {
        std::set<std::string> dependencies;
        for (const auto& guid : entry->dependencies)
        {
            std::string path;
            if (!GuidMap::valid_guid(guid) || !dependencies.insert(guid).second ||
                guid == entry->guid ||
                (!candidate.identities.guid_to_path(guid, path) &&
                 !builtin_identities().guid_to_path(guid, path)))
            {
                return false;
            }
        }
    }
    output = std::move(candidate);
    return true;
}

bool ResourceSystem::path_to_guid(std::string_view path, Guid& output)
{
    return active_catalog.identities.path_to_guid(path, output) ||
           builtin_identities().path_to_guid(path, output);
}

bool ResourceSystem::guid_to_path(std::string_view guid, std::string& output)
{
    return active_catalog.identities.guid_to_path(guid, output) ||
           builtin_identities().guid_to_path(guid, output);
}

bool ResourceSystem::register_mesh(const schema::MeshDataT& data)
{
    if (data.schema_version != 1 || !valid_resource_path(data.path) || meshes.contains(data.path) ||
        materials.contains(data.path) || shaders.contains(data.path) || data.vertices.empty() ||
        data.indices.empty() || data.indices.size() % 3 != 0 ||
        data.vertices.size() > std::numeric_limits<std::uint32_t>::max())
    {
        return false;
    }
    for (const auto& vertex : data.vertices)
    {
        if (!std::isfinite(vertex.x()) || !std::isfinite(vertex.y()) ||
            !std::isfinite(vertex.z()) || !std::isfinite(vertex.nx()) ||
            !std::isfinite(vertex.ny()) || !std::isfinite(vertex.nz()) ||
            (vertex.nx() == 0 && vertex.ny() == 0 && vertex.nz() == 0))
        {
            return false;
        }
    }
    if (std::any_of(data.indices.begin(), data.indices.end(),
                    [&data](std::uint32_t index) { return index >= data.vertices.size(); }))
    {
        return false;
    }
    meshes.emplace(data.path, std::make_unique<schema::MeshDataT>(data));
    return true;
}

const schema::MeshDataT* ResourceSystem::find_mesh(std::string_view path) noexcept
{
    const auto item = meshes.find(path);
    return item == meshes.end() ? nullptr : item->second.get();
}

bool ResourceSystem::activate_catalog(const ResourceCatalog& catalog)
{
    // 工作表复制后才切换，失败分支恢复全部旧定义；静态注册接口仍统一执行语义校验。
    auto old_shaders = std::move(shaders);
    auto old_materials = std::move(materials);
    auto old_meshes = std::move(meshes);
    auto old_catalog = std::move(active_catalog);
    shaders.clear();
    materials.clear();
    meshes.clear();
    const auto move_builtin = [](auto& source, auto& destination)
    {
        for (auto item = source.begin(); item != source.end();)
        {
            auto current = item++;
            if (current->first.starts_with("builtin/"))
            {
                destination.insert(source.extract(current));
            }
        }
    };
    move_builtin(old_shaders, shaders);
    move_builtin(old_materials, materials);
    move_builtin(old_meshes, meshes);
    active_catalog = catalog;
    bool success = catalog.data.schema_version == 1 && !catalog.data.revision.empty() &&
                   catalog.data.target_platform == target_platform() &&
                   catalog.data.entries.size() == catalog.identities.size() &&
                   catalog.data.entries.size() == catalog.artifacts.size() &&
                   catalog.data.entries.size() <= 65536;
    std::set<std::string> guids;
    std::set<std::string> modules;
    std::size_t total_size = 0;
    constexpr std::size_t maximum_snapshot_size = 512U * 1024U * 1024U;
    for (const auto& entry : catalog.data.entries)
    {
        if (!success || !entry)
        {
            success = false;
            break;
        }
        const auto bytes = catalog.artifacts.find(entry->guid);
        std::string mapped;
        std::string builtin_path;
        if (!entry->path.starts_with("project/") || !guids.insert(entry->guid).second ||
            builtin_identities().guid_to_path(entry->guid, builtin_path) ||
            !catalog.identities.guid_to_path(entry->guid, mapped) || mapped != entry->path ||
            entry->kind < schema::AssetKind::MIN || entry->kind > schema::AssetKind::MAX ||
            entry->content_size > static_cast<std::uint64_t>(maximum_read_size) ||
            entry->content_size > maximum_snapshot_size - total_size ||
            bytes == catalog.artifacts.end() || !matches_content(bytes->second, *entry))
        {
            success = false;
            break;
        }
        total_size += static_cast<std::size_t>(entry->content_size);
        if (entry->kind == schema::AssetKind::script &&
            (!valid_resource_path("project/" + entry->module_path) ||
             !entry->module_path.ends_with(".js") || !modules.insert(entry->module_path).second))
        {
            success = false;
            break;
        }
        std::set<std::string> dependencies;
        for (const auto& guid : entry->dependencies)
        {
            std::string dependency_path;
            if (!dependencies.insert(guid).second || guid == entry->guid ||
                !guid_to_path(guid, dependency_path))
            {
                success = false;
            }
        }
        if (!success)
        {
            break;
        }
        const auto* pointer = reinterpret_cast<const std::uint8_t*>(bytes->second.data());
        flatbuffers::Verifier verifier(pointer, bytes->second.size());
        if (entry->kind == schema::AssetKind::shader)
        {
            if (!schema::VerifyShaderDataBuffer(verifier))
            {
                success = false;
                break;
            }
            auto data = schema::UnPackShaderData(pointer);
            if (data->path != entry->path || !register_shader(*data))
            {
                success = false;
                break;
            }
        }
        else if (entry->kind == schema::AssetKind::mesh)
        {
            if (!schema::VerifyMeshDataBuffer(verifier))
            {
                success = false;
                break;
            }
            auto data = schema::UnPackMeshData(pointer);
            if (data->path != entry->path || !register_mesh(*data))
            {
                success = false;
                break;
            }
        }
        else if (entry->kind == schema::AssetKind::scene)
        {
            if (!schema::VerifySceneDataBuffer(verifier) ||
                schema::GetSceneData(pointer)->schema_version() != 1)
            {
                success = false;
                break;
            }
        }
    }
    if (success)
    {
        for (const auto& entry : catalog.data.entries)
        {
            if (entry->kind != schema::AssetKind::material)
            {
                continue;
            }
            const auto& bytes = catalog.artifacts.find(entry->guid)->second;
            const auto* pointer = reinterpret_cast<const std::uint8_t*>(bytes.data());
            flatbuffers::Verifier verifier(pointer, bytes.size());
            if (!schema::VerifyMaterialDataBuffer(verifier))
            {
                success = false;
                break;
            }
            auto data = schema::UnPackMaterialData(pointer);
            std::string shader_path;
            if (data->type_path != entry->path || !guid_to_path(data->shader_guid, shader_path) ||
                (!data->shader_path.empty() && data->shader_path != shader_path))
            {
                success = false;
                break;
            }
            data->shader_path = shader_path;
            if (!register_material(*data))
            {
                success = false;
                break;
            }
        }
    }
    if (!success)
    {
        move_builtin(shaders, old_shaders);
        move_builtin(materials, old_materials);
        move_builtin(meshes, old_meshes);
        shaders = std::move(old_shaders);
        materials = std::move(old_materials);
        meshes = std::move(old_meshes);
        active_catalog = std::move(old_catalog);
    }
    return success;
}

bool ResourceSystem::read_scene(std::string_view path, schema::SceneDataT& output)
{
    output = {};
    Guid guid;
    if (!path_to_guid(path, guid))
    {
        return false;
    }
    const auto* entry = active_catalog.find(guid);
    const auto bytes = active_catalog.artifacts.find(guid);
    if (entry == nullptr || entry->kind != schema::AssetKind::scene ||
        bytes == active_catalog.artifacts.end())
    {
        return false;
    }
    flatbuffers::Verifier verifier(reinterpret_cast<const std::uint8_t*>(bytes->second.data()),
                                   bytes->second.size());
    if (!schema::VerifySceneDataBuffer(verifier))
    {
        return false;
    }
    auto data = schema::UnPackSceneData(bytes->second.data());
    if (data->schema_version != 1)
    {
        return false;
    }
    output = std::move(*data);
    return true;
}

bool ResourceSystem::register_shader(const schema::ShaderDataT& data)
{
    if (!valid_resource_path(data.path) || shaders.contains(data.path) ||
        materials.contains(data.path) || meshes.contains(data.path) || data.schema_version != 1 ||
        data.vertex_code.empty() || data.fragment_code.empty())
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
    shaders.emplace(data.path, std::make_unique<schema::ShaderDataT>(data));
    return true;
}

bool ResourceSystem::register_material(const schema::MaterialDataT& data)
{
    if (!valid_resource_path(data.type_path) || shaders.contains(data.type_path) ||
        materials.contains(data.type_path) || meshes.contains(data.type_path) ||
        data.schema_version != 1)
    {
        return false;
    }
    const schema::ShaderDataT* shader = find_shader(data.shader_path);
    if (shader == nullptr ||
        (data.type_path.starts_with("builtin/") && data.shader_path.starts_with("project/")))
    {
        return false;
    }
    std::set<std::string_view> names;
    for (const auto& parameter : data.parameters)
    {
        if (parameter == nullptr || !names.insert(parameter->name).second)
        {
            return false;
        }
        const auto definition =
            std::find_if(shader->parameters.begin(), shader->parameters.end(),
                         [&parameter](const auto& item) { return item->name == parameter->name; });
        if (definition == shader->parameters.end() || !(*definition)->exposed ||
            !valid_parameter_value(**definition, parameter->value))
        {
            return false;
        }
    }
    materials.emplace(data.type_path, std::make_unique<schema::MaterialDataT>(data));
    return true;
}

const schema::ShaderDataT* ResourceSystem::find_shader(std::string_view path) noexcept
{
    const auto iterator = shaders.find(path);
    return iterator == shaders.end() ? nullptr : iterator->second.get();
}

const schema::MaterialDataT* ResourceSystem::find_material(std::string_view path) noexcept
{
    const auto iterator = materials.find(path);
    return iterator == materials.end() ? nullptr : iterator->second.get();
}

void ResourceSystem::clear_project_definitions() noexcept
{
    active_catalog = {};
    std::erase_if(meshes, [](const auto& item) { return item.first.starts_with("project/"); });
    std::erase_if(materials, [](const auto& item) { return item.first.starts_with("project/"); });
    std::erase_if(shaders, [](const auto& item) { return item.first.starts_with("project/"); });
}

void ResourceSystem::clear_definitions() noexcept
{
    active_catalog = {};
    meshes.clear();
    materials.clear();
    shaders.clear();
}

bool ResourceSystem::on_initialize(const EngineConfig&)
{
    if (definition_owner != nullptr)
    {
        return false;
    }
    // 转码查找表属于 Basis Universal，不需要单独销毁，重复初始化安全。
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
    if (!register_builtin_render_resources())
    {
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
        clear_definitions();
        definition_owner = nullptr;
    }
}

} // namespace engine
