#include "runtime/resource/resource_system.hpp"

#include "runtime/resource/builtin_resources.hpp"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_properties.h>
#include <basisu/transcoder/basisu_transcoder.h>
#include <flatbuffers/idl.h>

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

bool ResourceSystem::register_shader(const schema::ShaderDataT& data)
{
    if (!valid_resource_path(data.path) || shaders.contains(data.path) ||
        materials.contains(data.path) || data.vertex_code.empty() || data.fragment_code.empty())
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
        materials.contains(data.type_path))
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
    std::erase_if(materials, [](const auto& item) { return item.first.starts_with("project/"); });
    std::erase_if(shaders, [](const auto& item) { return item.first.starts_with("project/"); });
}

void ResourceSystem::clear_definitions() noexcept
{
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
    if (!register_builtin_render_resources())
    {
        std::erase_if(materials,
                      [&old_paths](const auto& item) { return !old_paths.contains(item.first); });
        std::erase_if(shaders,
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
