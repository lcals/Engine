#include "editor/asset_pipeline/asset_io.hpp"
#include "runtime/resource/virtual_file_system.hpp"

#include "runtime/resource/resource_system.hpp"
#include "runtime/utils/path_utils.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace engine::editor::asset_pipeline
{
namespace
{
namespace fs = std::filesystem;

bool unused_file(const fs::path& path, std::string& error)
{
    std::error_code status_error;
    const auto status = fs::symlink_status(path, status_error);
    if ((status_error && status_error != std::errc::no_such_file_or_directory) ||
        (!status_error && status.type() != fs::file_type::not_found))
    {
        error = "Source or meta already exists, or its destination cannot be inspected: " +
                PathUtils::to_utf8(path);
        return false;
    }
    return true;
}
} // namespace

std::string hash_bytes(std::span<const std::byte> bytes)
{
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto value : bytes)
    {
        hash ^= std::to_integer<unsigned char>(value);
        hash *= 1099511628211ULL;
    }
    std::array<char, 17> text{};
    std::snprintf(text.data(), text.size(), "%016llx", static_cast<unsigned long long>(hash));
    return text.data();
}

std::string hash_text(const std::string& text)
{
    return hash_bytes(std::as_bytes(std::span(text.data(), text.size())));
}

bool read_bytes(const std::filesystem::path& path, std::vector<std::byte>& bytes,
                std::string& error)
{
    if (!VirtualFileSystem::read_file(path, bytes))
    {
        error = "cannot read file: " + PathUtils::to_utf8(path);
        return false;
    }
    return true;
}

bool write_bytes(const std::filesystem::path& path, std::span<const std::byte> bytes,
                 std::string& error)
{
    SDL_IOStream* stream = SDL_IOFromFile(PathUtils::to_utf8(path).c_str(), "wb");
    if (stream == nullptr)
    {
        error = "cannot create file: " + PathUtils::to_utf8(path) + ": " + SDL_GetError();
        return false;
    }
    const bool written =
        bytes.empty() || SDL_WriteIO(stream, bytes.data(), bytes.size()) == bytes.size();
    const bool flushed = written && SDL_FlushIO(stream);
    const bool closed = SDL_CloseIO(stream);
    if (!written || !flushed || !closed)
    {
        error = "cannot finish file: " + PathUtils::to_utf8(path) + ": " + SDL_GetError();
        return false;
    }
    return true;
}

bool write_text(const std::filesystem::path& path, const std::string& text, std::string& error)
{
    return write_bytes(path, std::as_bytes(std::span(text.data(), text.size())), error);
}

bool project_file_path(const fs::path& project, std::string_view directory,
                       std::string_view relative, std::string_view extension, fs::path& output,
                       std::string& error)
{
    output.clear();
    if (relative.empty() || relative.size() > 1024 ||
        std::any_of(relative.begin(), relative.end(), [](unsigned char value)
                    { return value < 0x20 || value == 0x7f || value == ':' || value == '\\'; }))
    {
        error = "Project file path requires a bounded relative path without control characters";
        return false;
    }
    const auto path = PathUtils::from_utf8(relative);
    if (path.is_absolute() || path.has_root_name() || path.filename().empty() ||
        PathUtils::to_utf8(path.extension()) != extension ||
        std::any_of(path.begin(), path.end(),
                    [](const auto& part) { return part == ".." || part == "." || part.empty(); }))
    {
        error = "File path must stay in its project directory and use " + std::string(extension);
        return false;
    }
    std::error_code filesystem_error;
    const auto root = fs::canonical(project, filesystem_error);
    if (filesystem_error || !fs::is_directory(root, filesystem_error) || filesystem_error)
    {
        error = "Project directory is unavailable";
        return false;
    }
    const auto candidate = root / PathUtils::from_utf8(directory) / path;
    fs::path cursor = root;
    for (const auto& part : candidate.lexically_relative(root))
    {
        cursor /= part;
        const auto status = fs::symlink_status(cursor, filesystem_error);
        if (filesystem_error && filesystem_error != std::errc::no_such_file_or_directory)
        {
            error = "Cannot inspect project file destination";
            return false;
        }
        filesystem_error.clear();
        if (fs::is_symlink(status) || (status.type() != fs::file_type::not_found &&
                                       cursor != candidate && !fs::is_directory(status)))
        {
            error = "Project file destination contains a symbolic link or non-directory parent";
            return false;
        }
    }
    output = candidate;
    return true;
}

bool create_source_with_meta(const fs::path& destination, const std::string& source,
                             std::string_view importer, std::string& guid, std::string& error)
{
    guid.clear();
    auto meta = destination;
    meta += ".meta";
    if (!unused_file(destination, error) || !unused_file(meta, error))
    {
        return false;
    }
    std::error_code filesystem_error;
    fs::create_directories(destination.parent_path(), filesystem_error);
    if (filesystem_error)
    {
        error = "Cannot create source directory: " + filesystem_error.message();
        return false;
    }
    const auto candidate_guid = new_guid();
    if (candidate_guid.empty())
    {
        error = "Cannot generate resource GUID";
        return false;
    }
    auto source_temporary = destination;
    source_temporary += ".asset-write-" + candidate_guid + ".meta";
    auto meta_temporary = meta;
    meta_temporary += ".asset-write-" + candidate_guid + ".meta";
    if (!unused_file(source_temporary, error) || !unused_file(meta_temporary, error))
    {
        return false;
    }
    const auto metadata = "guid = \"" + candidate_guid + "\"\nimporter = \"" +
                          std::string(importer) + "\"\nmeta_version = 1\n";
    const auto cleanup = [&]()
    {
        std::error_code ignored;
        fs::remove(source_temporary, ignored);
        fs::remove(meta_temporary, ignored);
    };
    if (!write_text(source_temporary, source, error) ||
        !write_text(meta_temporary, metadata, error))
    {
        cleanup();
        return false;
    }
    if (!SDL_RenamePath(PathUtils::to_utf8(meta_temporary).c_str(),
                        PathUtils::to_utf8(meta).c_str()))
    {
        cleanup();
        error = "Cannot publish source meta: " + std::string(SDL_GetError());
        return false;
    }
    if (!SDL_RenamePath(PathUtils::to_utf8(source_temporary).c_str(),
                        PathUtils::to_utf8(destination).c_str()))
    {
        cleanup();
        fs::remove(meta, filesystem_error);
        error = "Cannot publish source: " + std::string(SDL_GetError());
        if (filesystem_error)
        {
            error += "; newly created meta could not be removed";
        }
        return false;
    }
    guid = candidate_guid;
    return true;
}

bool run_process(const std::vector<std::string>& arguments, std::string& error,
                 std::string* captured_output)
{
    std::vector<const char*> argv;
    argv.reserve(arguments.size() + 1);
    for (const auto& argument : arguments)
    {
        argv.push_back(argument.c_str());
    }
    argv.push_back(nullptr);
    const SDL_PropertiesID properties = SDL_CreateProperties();
    const bool configured =
        properties != 0 &&
        SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv.data()) &&
        SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER,
                              SDL_PROCESS_STDIO_NULL) &&
        SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,
                              SDL_PROCESS_STDIO_APP) &&
        SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
    SDL_Process* process = configured ? SDL_CreateProcessWithProperties(properties) : nullptr;
    SDL_DestroyProperties(properties);
    if (process == nullptr)
    {
        error = "cannot start tool: " + (arguments.empty() ? std::string{} : arguments[0]) + ": " +
                SDL_GetError();
        return false;
    }
    std::size_t output_size = 0;
    int exit_code = -1;
    void* output = SDL_ReadProcess(process, &output_size, &exit_code);
    std::string diagnostics;
    if (output != nullptr)
    {
        diagnostics.assign(static_cast<const char*>(output), output_size);
    }
    SDL_free(output);
    const bool waited = SDL_WaitProcess(process, true, &exit_code);
    SDL_DestroyProcess(process);
    if (!waited || exit_code != 0)
    {
        error = "tool failed (exit " + std::to_string(exit_code) + "): " + diagnostics;
        return false;
    }
    if (captured_output != nullptr)
    {
        *captured_output = std::move(diagnostics);
    }
    return true;
}

std::string new_guid()
{
    // GUID 是身份，不是凭据；SDL 自带的跨平台随机源，生成后还由映射和 SQL 双重查重。
    std::array<unsigned char, 16> bytes{};
    std::uint64_t state = SDL_GetPerformanceCounter() ^ SDL_GetTicksNS();
    for (auto& value : bytes)
    {
        value = static_cast<unsigned char>(SDL_rand_bits_r(&state));
    }
    bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0fU) | 0x40U);
    bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3fU) | 0x80U);
    std::array<char, 37> text{};
    std::snprintf(text.data(), text.size(),
                  "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", bytes[0],
                  bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7], bytes[8],
                  bytes[9], bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
    return text.data();
}

} // namespace engine::editor::asset_pipeline
