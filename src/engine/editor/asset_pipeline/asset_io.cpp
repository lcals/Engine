#include "editor/asset_pipeline/asset_io.hpp"

#include "runtime/resource/resource_system.hpp"
#include "runtime/utils/path_utils.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace engine::asset_pipeline
{

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
    if (!ResourceSystem::read_binary(path, bytes))
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

} // namespace engine::asset_pipeline
