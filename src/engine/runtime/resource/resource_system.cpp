#include "runtime/resource/resource_system.hpp"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_properties.h>
#include <basisu/transcoder/basisu_transcoder.h>
#include <flatbuffers/idl.h>

#include <string_view>

namespace engine
{
namespace
{

constexpr Sint64 maximum_read_size = 64 * 1024 * 1024;

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

bool ResourceSystem::on_initialize(const EngineConfig&)
{
    // 转码查找表属于 Basis Universal，不需要单独销毁，重复初始化安全。
    basist::basisu_transcoder_init();

    // 以私有最小 schema 验证解析入口；该探针不定义正式资源格式。
    flatbuffers::Parser parser;
    return parser.Parse("table DependencyProbe { value:uint; } root_type DependencyProbe;");
}

void ResourceSystem::on_shutdown() noexcept
{
}

} // namespace engine
