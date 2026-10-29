#include "runtime/resource/virtual_file_system.hpp"

#include "runtime/schema/asset_catalog_generated.h"
#include "runtime/utils/path_utils.hpp"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_properties.h>

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <set>
#include <string_view>
#include <system_error>

namespace engine
{
namespace
{

constexpr std::uint64_t maximum_read_size = 64U * 1024U * 1024U;
using Stream = std::unique_ptr<SDL_IOStream, decltype(&SDL_CloseIO)>;

bool valid_relative_path(std::string_view path)
{
    for (const unsigned char character : path)
    {
        if (character < 32 || character == 127 || character == '\\' || character == ':')
        {
            return false;
        }
    }
    for (;;)
    {
        const auto slash = path.find('/');
        const auto segment = path.substr(0, slash);
        if (segment.empty() || segment == "." || segment == "..")
        {
            return false;
        }
        if (slash == std::string_view::npos)
        {
            return true;
        }
        path.remove_prefix(slash + 1);
    }
}

bool apk_root(const std::filesystem::path& path)
{
    const auto encoded = path.generic_u8string();
    return encoded == u8"apk:/assets" || encoded.starts_with(u8"apk:/assets/");
}

bool contained_file(const std::filesystem::path& root, const std::filesystem::path& relative)
{
    if (apk_root(root))
    {
        return true;
    }
    std::error_code error;
    const auto root_status = std::filesystem::symlink_status(root, error);
    if (error || !std::filesystem::is_directory(root_status) ||
        std::filesystem::is_symlink(root_status))
    {
        return false;
    }
    auto current = root;
    for (const auto& segment : relative)
    {
        current /= segment;
        const auto status = std::filesystem::symlink_status(current, error);
        if (error || std::filesystem::is_symlink(status))
        {
            return false;
        }
    }
    return std::filesystem::is_regular_file(std::filesystem::symlink_status(current, error)) &&
           !error;
}

Stream open_file(const std::filesystem::path& path)
{
    Stream stream(nullptr, SDL_CloseIO);
    const auto encoded = path.u8string();
    if (encoded.empty() || encoded.find(u8'\0') != std::u8string::npos)
    {
        return stream;
    }
    std::string filename(encoded.begin(), encoded.end());
    const bool apk = filename.starts_with("apk:");
    if (apk)
    {
        constexpr std::string_view prefix = "apk:/assets/";
        if (!filename.starts_with(prefix) ||
            !valid_relative_path(std::string_view(filename).substr(prefix.size())))
        {
            return stream;
        }
#if defined(SDL_PLATFORM_ANDROID)
        filename.erase(0, prefix.size());
#else
        return stream;
#endif
    }
    else
    {
        SDL_PathInfo info{};
        if (!SDL_GetPathInfo(filename.c_str(), &info) || info.type != SDL_PATHTYPE_FILE)
        {
            return stream;
        }
    }
    stream.reset(SDL_IOFromFile(filename.c_str(), "rb"));
#if defined(SDL_PLATFORM_ANDROID)
    if (stream && apk &&
        SDL_GetPointerProperty(SDL_GetIOProperties(stream.get()),
                               SDL_PROP_IOSTREAM_ANDROID_AASSET_POINTER, nullptr) == nullptr)
    {
        // SDL 优先查私有目录；内置逻辑路径只接受真实的 AAsset。
        stream.reset();
    }
#endif
    return stream;
}

bool read_exact(SDL_IOStream* stream, ByteBuffer& output)
{
    std::size_t position = 0;
    while (position < output.size())
    {
        const auto count = SDL_ReadIO(stream, output.data() + position, output.size() - position);
        if (count == 0)
        {
            return false;
        }
        position += count;
    }
    return true;
}

} // namespace

bool VirtualFileSystem::configure(const MountList& mounts)
{
    std::set<std::string> names;
    for (const auto& mount : mounts)
    {
        const auto encoded = mount.root.generic_u8string();
        if (!valid_relative_path(mount.name) || mount.name.find('/') != std::string::npos ||
            !names.insert(mount.name).second || encoded.empty() ||
            encoded.find(u8'\0') != std::u8string::npos)
        {
            return false;
        }
        if (encoded.starts_with(u8"apk:"))
        {
            if (!apk_root(mount.root))
            {
                return false;
            }
            constexpr std::size_t prefix_size = sizeof("apk:/assets/") - 1;
            if (encoded.size() >= prefix_size)
            {
                const std::string remainder(encoded.begin() + prefix_size, encoded.end());
                if (!valid_relative_path(remainder))
                {
                    return false;
                }
            }
        }
    }
    mounts_ = mounts;
    return true;
}

bool VirtualFileSystem::read(const schema::FileLocationT& location, std::uint64_t offset,
                             std::uint64_t size, ByteBuffer& output) const
{
    output.clear();
    const auto mount = std::find_if(mounts_.begin(), mounts_.end(), [&location](const auto& value)
                                    { return value.name == location.mount; });
    constexpr auto maximum_offset = static_cast<std::uint64_t>(std::numeric_limits<Sint64>::max());
    if (mount == mounts_.end() || !valid_relative_path(location.path) || offset > location.length ||
        size > location.length - offset || size > maximum_read_size ||
        location.offset > maximum_offset || location.length > maximum_offset - location.offset)
    {
        return false;
    }
    const std::filesystem::path relative = PathUtils::from_utf8(location.path);
    if (!contained_file(mount->root, relative))
    {
        return false;
    }
    auto stream = open_file(mount->root / relative);
    if (!stream)
    {
        return false;
    }
    const Sint64 file_size = SDL_GetIOSize(stream.get());
    const auto start = static_cast<Sint64>(location.offset + offset);
    if (file_size < 0 ||
        static_cast<std::uint64_t>(file_size) < location.offset + location.length ||
        SDL_SeekIO(stream.get(), start, SDL_IO_SEEK_SET) != start)
    {
        return false;
    }
    output.resize(static_cast<std::size_t>(size));
    const bool success = read_exact(stream.get(), output);
    const bool closed = SDL_CloseIO(stream.release());
    if (!success || !closed)
    {
        output.clear();
        return false;
    }
    return true;
}

bool VirtualFileSystem::read_file(const std::filesystem::path& path, ByteBuffer& output)
{
    output.clear();
    auto stream = open_file(path);
    if (!stream)
    {
        return false;
    }
    const Sint64 size = SDL_GetIOSize(stream.get());
    if (size < 0 || static_cast<std::uint64_t>(size) > maximum_read_size)
    {
        return false;
    }
    output.resize(static_cast<std::size_t>(size));
    bool success = read_exact(stream.get(), output);
    if (success)
    {
        std::byte extra{};
        success = SDL_ReadIO(stream.get(), &extra, 1) == 0 &&
                  SDL_GetIOStatus(stream.get()) == SDL_IO_STATUS_EOF;
    }
    const bool closed = SDL_CloseIO(stream.release());
    if (!success || !closed)
    {
        output.clear();
        return false;
    }
    return true;
}

bool VirtualFileSystem::hash_file(const std::filesystem::path& path, std::uint64_t& hash,
                                  std::uint64_t& size)
{
    hash = 0;
    size = 0;
    auto stream = open_file(path);
    if (!stream)
    {
        return false;
    }
    const Sint64 expected_size = SDL_GetIOSize(stream.get());
    if (expected_size < 0)
    {
        return false;
    }
    const auto expected = static_cast<std::uint64_t>(expected_size);
    std::array<std::byte, 64U * 1024U> buffer{};
    std::uint64_t checksum = 14695981039346656037ULL;
    std::uint64_t position = 0;
    bool success = false;
    for (;;)
    {
        const auto count = SDL_ReadIO(stream.get(), buffer.data(), buffer.size());
        if (count == 0)
        {
            success = position == expected && SDL_GetIOStatus(stream.get()) == SDL_IO_STATUS_EOF;
            break;
        }
        if (count > expected - position)
        {
            break;
        }
        position += count;
        for (std::size_t index = 0; index < count; ++index)
        {
            checksum ^= std::to_integer<std::uint8_t>(buffer[index]);
            checksum *= 1099511628211ULL;
        }
    }
    const bool closed = SDL_CloseIO(stream.release());
    if (!success || !closed)
    {
        return false;
    }
    hash = checksum;
    size = position;
    return true;
}

const MountList& VirtualFileSystem::mounts() const noexcept
{
    return mounts_;
}

} // namespace engine
