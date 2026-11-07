#include "editor/asset_pipeline/import_tool.hpp"

#include "editor/asset_pipeline/editor_tool_directories.hpp"
#include "editor/asset_pipeline/editor_tools.hpp"
#include "runtime/utils/path_utils.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstdlib>
#include <system_error>

namespace engine::editor::asset_pipeline
{
bool find_import_tool(std::string_view name, std::filesystem::path& output, std::string& error)
{
    output.clear();
    error.clear();
    if (name.empty() ||
        name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-_") != std::string_view::npos)
    {
        error = "invalid import tool name";
        return false;
    }
    std::filesystem::path executables;
    std::filesystem::path data;
    if (!editor_tool_directories(executables, data, error))
    {
        return false;
    }
    std::string filename(name);
    if (std::string_view(SDL_GetPlatform()) == "Windows")
    {
        filename += ".exe";
    }
    std::filesystem::path candidate;
    if (!executables.empty())
    {
        candidate = executables / PathUtils::from_utf8(filename);
    }
    else if (const char* configured = std::getenv("ENGINE_IMPORT_TOOL_DIR");
             configured != nullptr && *configured != '\0')
    {
        candidate = PathUtils::from_utf8(configured) / PathUtils::from_utf8(filename);
    }
    else if (name == "ffmpeg")
    {
        candidate = PathUtils::from_utf8(ENGINE_FFMPEG_EXECUTABLE);
    }
    else if (name == "ffprobe")
    {
        candidate = PathUtils::from_utf8(ENGINE_FFPROBE_EXECUTABLE);
    }
    std::error_code code;
    if (!std::filesystem::is_regular_file(candidate, code) || code)
    {
        error = "import tool is missing: " + filename;
        return false;
    }
    output = candidate;
    return true;
}

ImportToolStatus run_import_tool(const std::vector<std::string>& arguments,
                                 const CancellationCheck& cancel_requested,
                                 std::size_t max_output_bytes, std::string& output,
                                 std::string& error)
{
    output.clear();
    error.clear();
    if (arguments.empty() || arguments.front().empty() || max_output_bytes == 0)
    {
        error = "invalid import tool arguments or output limit";
        return ImportToolStatus::failed;
    }
    if (cancel_requested && cancel_requested())
    {
        return ImportToolStatus::cancelled;
    }
    std::vector<const char*> argv;
    argv.reserve(arguments.size() + 1);
    for (const auto& argument : arguments)
    {
        argv.push_back(argument.c_str());
    }
    argv.push_back(nullptr);
    const auto properties = SDL_CreateProperties();
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
        error = std::string("cannot start import tool: ") + SDL_GetError();
        return ImportToolStatus::failed;
    }
    SDL_IOStream* stream = SDL_GetProcessOutput(process);
    ImportToolStatus result = ImportToolStatus::failed;
    bool stop = stream == nullptr;
    if (stop)
    {
        error = std::string("cannot read import tool output: ") + SDL_GetError();
    }
    bool exited = false;
    bool ended = false;
    int exit_code = -1;
    std::array<char, 4096> buffer{};
    while (!stop && !(exited && ended))
    {
        if (cancel_requested && cancel_requested())
        {
            result = ImportToolStatus::cancelled;
            stop = true;
            break;
        }
        const auto count = SDL_ReadIO(stream, buffer.data(), buffer.size());
        if (count > max_output_bytes - output.size())
        {
            error = "import tool output exceeds limit";
            stop = true;
            break;
        }
        output.append(buffer.data(), count);
        const auto status = SDL_GetIOStatus(stream);
        if (status == SDL_IO_STATUS_ERROR)
        {
            error = std::string("cannot read import tool output: ") + SDL_GetError();
            stop = true;
            break;
        }
        ended = status == SDL_IO_STATUS_EOF;
        if (!exited)
        {
            exited = SDL_WaitProcess(process, false, &exit_code);
        }
        if (!count && !(exited && ended))
        {
            SDL_Delay(5);
        }
    }
    if (stop && !exited)
    {
        if (!SDL_KillProcess(process, true))
        {
            error = std::string("cannot stop import tool: ") + SDL_GetError();
            result = ImportToolStatus::failed;
        }
    }
    if (!exited && !SDL_WaitProcess(process, true, &exit_code))
    {
        error = std::string("cannot wait for import tool: ") + SDL_GetError();
        result = ImportToolStatus::failed;
    }
    SDL_DestroyProcess(process);
    if (!stop)
    {
        if (exit_code == 0)
        {
            result = ImportToolStatus::success;
        }
        else
        {
            error = "import tool failed with exit code " + std::to_string(exit_code);
        }
    }
    return result;
}
} // namespace engine::editor::asset_pipeline
