#include "ios/editor/plugins/mcp/ios_container.hpp"

#include "editor/asset_pipeline/project_lock.hpp"
#include "plugins/editor/mcp/mcp_target.hpp"
#include "plugins/editor/mcp/project_tool_utils.hpp"
#include "runtime/utils/hash_utils.hpp"
#include "runtime/utils/path_utils.hpp"

#include <SDL3/SDL.h>
#include <libimobiledevice/afc.h>
#include <libimobiledevice/house_arrest.h>
#include <libimobiledevice/libimobiledevice.h>
#include <nlohmann/json.hpp>
#include <plist/plist.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <new>
#include <system_error>
#include <vector>

namespace engine::plugins::mcp::ios_detail
{
namespace
{
namespace fs = std::filesystem;
using Json = nlohmann::json;
constexpr std::size_t chunk_size = 1024 * 1024;
enum class FileKind
{
    missing,
    file,
    directory
};

bool relative_path(std::string_view path)
{
    if (path.empty() || path.front() == '/' || path.back() == '/' ||
        path.find_first_of("\\:") != path.npos || path.find('\0') != path.npos)
    {
        return false;
    }
    std::size_t first = 0;
    for (;;)
    {
        const auto end = path.find('/', first);
        const auto component = path.substr(first, end == path.npos ? end : end - first);
        if (component.empty() || component == "." || component == "..")
        {
            return false;
        }
        if (end == path.npos)
        {
            return true;
        }
        first = end + 1;
    }
}

bool afc_result(afc_error_t result, std::string_view operation, std::string& error)
{
    if (result == AFC_E_SUCCESS)
    {
        return true;
    }
    error = "iOS app-container " + std::string(operation) + " failed (AFC " +
            std::to_string(static_cast<int>(result)) + ")";
    return false;
}

bool remote_info(afc_client_t client, const std::string& path, FileKind& kind, std::uint64_t& size,
                 std::string& error)
{
    kind = FileKind::missing;
    size = 0;
    char** values = nullptr;
    const auto result = afc_get_file_info(client, path.c_str(), &values);
    if (result == AFC_E_OBJECT_NOT_FOUND)
    {
        if (values)
        {
            afc_dictionary_free(values);
        }
        return true;
    }
    bool valid = afc_result(result, "file inspection", error) && values != nullptr;
    std::string type;
    bool size_valid = false;
    if (values)
    {
        for (std::size_t index = 0; values[index] && values[index + 1]; index += 2)
        {
            const std::string_view key(values[index]);
            const std::string_view value(values[index + 1]);
            if (key == "st_ifmt")
            {
                type = value;
            }
            else if (key == "st_size")
            {
                const auto parsed =
                    std::from_chars(value.data(), value.data() + value.size(), size);
                size_valid = parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size();
            }
        }
        afc_dictionary_free(values);
    }
    if (valid && type == "S_IFDIR")
    {
        kind = FileKind::directory;
        return true;
    }
    if (valid && type == "S_IFREG" && size_valid)
    {
        kind = FileKind::file;
        return true;
    }
    if (error.empty())
    {
        error = "iOS app-container path is a symlink or unsupported file: " + path;
    }
    return false;
}

#if defined(SDL_PLATFORM_MACOS)
bool xcrun(const McpTarget& target, const std::vector<std::string>& arguments, std::string& output,
           std::string& error)
{
    output.clear();
    std::vector<std::string> command{PathUtils::to_utf8(target.xcrun_executable)};
    command.insert(command.end(), arguments.begin(), arguments.end());
    std::vector<const char*> argv;
    for (const auto& part : command)
    {
        argv.push_back(part.c_str());
    }
    argv.push_back(nullptr);
    auto* process = SDL_CreateProcess(argv.data(), true);
    if (!process)
    {
        error = "Cannot start iOS simulator tool: " + std::string(SDL_GetError());
        return false;
    }
    int code = -1;
    std::size_t count = 0;
    void* bytes = SDL_ReadProcess(process, &count, &code);
    if (bytes && count <= 1024 * 1024)
    {
        output.assign(static_cast<const char*>(bytes), count);
    }
    SDL_free(bytes);
    const bool waited = SDL_WaitProcess(process, true, &code);
    SDL_DestroyProcess(process);
    if (!waited || code != 0 || count > 1024 * 1024)
    {
        error =
            "iOS simulator command failed for " + target.ios_udid + ": " + output.substr(0, 8192);
        output.clear();
        return false;
    }
    return true;
}
#endif
} // namespace

struct IosContainer::State
{
    idevice_t device = nullptr;
    house_arrest_client_t house = nullptr;
    afc_client_t afc = nullptr;
    std::uint64_t lock_handle = 0;
    engine::editor::asset_pipeline::ProjectLock host_lock;
    fs::path host_root;
    std::string remote_root;
    std::string runtime_dir;
    bool simulator = false;

    ~State() noexcept
    {
        if (afc)
        {
            if (lock_handle != 0)
            {
                (void)afc_file_lock(afc, lock_handle, AFC_LOCK_UN);
                (void)afc_file_close(afc, lock_handle);
            }
            (void)afc_client_free(afc);
        }
        if (house)
        {
            (void)house_arrest_client_free(house);
        }
        if (device)
        {
            (void)idevice_free(device);
        }
    }

    bool resolve(std::string_view path, fs::path& local, std::string& remote,
                 std::string& error) const
    {
        if (!relative_path(path))
        {
            error = "Invalid app-relative iOS sync path";
            return false;
        }
        if (simulator)
        {
            local = host_root / PathUtils::from_utf8(path);
            if (!tools::no_symlinks(local))
            {
                error = "iOS simulator path contains a symlink or inaccessible entry";
                return false;
            }
            return true;
        }
        remote = remote_root + "/" + std::string(path);
        // 包含运行根在内逐级检查；不能只检查最后一个 GUID 文件。
        for (std::size_t end = 1;;)
        {
            end = remote.find('/', end);
            const auto component = remote.substr(0, end);
            FileKind kind;
            std::uint64_t size = 0;
            if (!remote_info(afc, component, kind, size, error) ||
                (end != std::string::npos && kind == FileKind::file))
            {
                if (error.empty())
                {
                    error = "An iOS sync parent is not a directory";
                }
                return false;
            }
            if (end == std::string::npos)
            {
                return true;
            }
            ++end;
        }
    }

    bool info(std::string_view path, FileKind& kind, std::uint64_t& size, std::string& error) const
    {
        kind = FileKind::missing;
        size = 0;
        fs::path local;
        std::string remote;
        if (!resolve(path, local, remote, error))
        {
            return false;
        }
        if (!simulator)
        {
            return remote_info(afc, remote, kind, size, error);
        }
        std::error_code code;
        const auto status = fs::symlink_status(local, code);
        if (code == std::errc::no_such_file_or_directory || (!code && !fs::exists(status)))
        {
            return true;
        }
        if (!code && fs::is_directory(status))
        {
            kind = FileKind::directory;
            return true;
        }
        if (!code && fs::is_regular_file(status))
        {
            size = fs::file_size(local, code);
            kind = FileKind::file;
        }
        if (code || kind == FileKind::missing)
        {
            error = "Cannot inspect the iOS simulator file";
            return false;
        }
        return true;
    }

    bool prepare_root(std::string& error)
    {
        if (simulator)
        {
            std::error_code code;
            if (!tools::no_symlinks(host_root))
            {
                error = "The iOS simulator persistent path contains an unsafe entry";
                return false;
            }
            fs::create_directories(host_root, code);
            if (code || !fs::is_directory(host_root, code) || code)
            {
                error = "Cannot prepare the iOS simulator persistent directory";
                return false;
            }
            return true;
        }
        for (const auto& path :
             {std::string("/Library"), std::string("/Library/Application Support"), remote_root})
        {
            FileKind kind;
            std::uint64_t size = 0;
            if (!remote_info(afc, path, kind, size, error) || kind == FileKind::file ||
                (kind == FileKind::missing &&
                 !afc_result(afc_make_directory(afc, path.c_str()), "directory creation", error)))
            {
                if (error.empty())
                {
                    error = "The iOS persistent path is not a directory";
                }
                return false;
            }
        }
        return true;
    }
};

IosContainer::IosContainer(const McpTarget& target) noexcept : target_(target)
{
}
IosContainer::~IosContainer() noexcept = default;

bool IosContainer::open(std::string& error)
{
    error.clear();
    if (state_)
    {
        error = "The iOS app container is already open";
        return false;
    }
    auto next = std::unique_ptr<State>(new (std::nothrow) State{});
    if (!next)
    {
        error = "Cannot allocate the iOS app-container connection";
        return false;
    }
    next->simulator = target_.ios_simulator;
    const auto relative = "Library/Application Support/" + target_.ios_application_name;
    if (next->simulator)
    {
#if !defined(SDL_PLATFORM_MACOS)
        error = "iOS simulator synchronization requires a macOS host";
        return false;
#else
        std::string output;
        if (!xcrun(target_, {"simctl", "list", "devices", "--json"}, output, error))
        {
            return false;
        }
        const auto list = Json::parse(output, nullptr, false);
        bool selected = false;
        if (list.is_object() && list.contains("devices") && list["devices"].is_object())
        {
            for (const auto& group : list["devices"])
            {
                if (!group.is_array())
                {
                    continue;
                }
                for (const auto& device : group)
                {
                    selected = selected || (device.is_object() &&
                                            device.value("udid", Json()) == target_.ios_udid &&
                                            device.value("state", Json()) == "Booted" &&
                                            device.value("isAvailable", Json(false)) == true);
                }
            }
        }
        if (!selected)
        {
            error = "The explicitly selected iOS simulator is not booted and available";
            return false;
        }
        if (!xcrun(target_,
                   {"simctl", "get_app_container", target_.ios_udid, target_.ios_bundle_identifier,
                    "data"},
                   output, error))
        {
            return false;
        }
        while (!output.empty() && (output.back() == '\n' || output.back() == '\r'))
        {
            output.pop_back();
        }
        const auto container = PathUtils::from_utf8(output);
        next->host_root = container / PathUtils::from_utf8(relative);
        std::error_code code;
        if (output.find_first_of("\r\n\0", 0, 3) != output.npos || !container.is_absolute() ||
            !tools::no_symlinks(next->host_root) || !fs::is_directory(container, code) || code)
        {
            error = "simctl returned an invalid application data container";
            return false;
        }
        next->runtime_dir = PathUtils::to_utf8(next->host_root);
#endif
    }
    else
    {
        const auto options =
            static_cast<idevice_options>(IDEVICE_LOOKUP_USBMUX | IDEVICE_LOOKUP_NETWORK);
        if (idevice_new_with_options(&next->device, target_.ios_udid.c_str(), options) !=
                IDEVICE_E_SUCCESS ||
            house_arrest_client_start_service(next->device, &next->house, "engine-resource-sync") !=
                HOUSE_ARREST_E_SUCCESS ||
            house_arrest_send_command(next->house, "VendContainer",
                                      target_.ios_bundle_identifier.c_str()) !=
                HOUSE_ARREST_E_SUCCESS)
        {
            error = "Cannot open the selected iOS development app container; check pairing, trust "
                    "and app access";
            return false;
        }
        plist_t reply = nullptr;
        const auto result = house_arrest_get_result(next->house, &reply);
        char* message = nullptr;
        if (reply)
        {
            const auto node = plist_dict_get_item(reply, "Error");
            if (node)
            {
                plist_get_string_val(node, &message);
            }
        }
        const bool accepted = result == HOUSE_ARREST_E_SUCCESS && reply &&
                              plist_get_node_type(reply) == PLIST_DICT &&
                              plist_dict_get_item(reply, "Error") == nullptr;
        if (!accepted)
        {
            error = "iOS rejected access to bundle " + target_.ios_bundle_identifier + ": " +
                    (message ? std::string(message) : "invalid House Arrest response");
        }
        std::free(message);
        if (reply)
        {
            plist_free(reply);
        }
        if (!accepted ||
            !afc_result(afc_client_new_from_house_arrest_client(next->house, &next->afc),
                        "connection", error))
        {
            return false;
        }
        next->remote_root = "/" + relative;
        next->runtime_dir = next->remote_root;
    }
    // 打开容器只查询。首次启动前的目录由后续写操作按需建立。
    fs::path local;
    std::string remote;
    if (!next->resolve("script-status.json", local, remote, error))
    {
        return false;
    }
    state_ = std::move(next);
    return true;
}

std::string_view IosContainer::runtime_dir() const noexcept
{
    return state_ ? std::string_view(state_->runtime_dir) : std::string_view{};
}

bool IosContainer::directory(std::string_view path, bool& exists, std::string& error)
{
    exists = false;
    FileKind kind;
    std::uint64_t size = 0;
    if (!state_ || !state_->info(path, kind, size, error))
    {
        return false;
    }
    if (kind == FileKind::file)
    {
        error = "The iOS version path is not a directory";
        return false;
    }
    exists = kind == FileKind::directory;
    return true;
}

bool IosContainer::make_directory(std::string_view path, std::string& error)
{
    if (!state_ || !relative_path(path))
    {
        error = "Invalid app-relative iOS directory";
        return false;
    }
    if (!state_->prepare_root(error))
    {
        return false;
    }
    for (std::size_t end = 0;;)
    {
        end = path.find('/', end);
        const auto component = path.substr(0, end);
        FileKind kind;
        std::uint64_t size = 0;
        fs::path local;
        std::string remote;
        if (!state_->info(component, kind, size, error) || kind == FileKind::file ||
            !state_->resolve(component, local, remote, error))
        {
            if (error.empty())
            {
                error = "An iOS sync parent is not a directory";
            }
            return false;
        }
        if (kind == FileKind::missing)
        {
            std::error_code code;
            const bool made = state_->simulator
                                  ? fs::create_directory(local, code)
                                  : afc_result(afc_make_directory(state_->afc, remote.c_str()),
                                               "directory creation", error);
            if (!made || code)
            {
                if (error.empty())
                {
                    error = "Cannot create the iOS app-container directory";
                }
                return false;
            }
        }
        if (end == path.npos)
        {
            return true;
        }
        ++end;
    }
}

bool IosContainer::lock(std::string& error)
{
    if (!make_directory("assets/.sync-lock", error))
    {
        return false;
    }
    fs::path local;
    std::string remote;
    if (!state_->resolve("assets/.sync-lock/owner.lock", local, remote, error))
    {
        return false;
    }
    if (state_->simulator)
    {
        bool acquired = false;
        if (!state_->host_lock.try_acquire(local, acquired, error) || !acquired)
        {
            if (error.empty())
            {
                error = "The iOS asset publication lock is already held";
            }
            return false;
        }
        return true;
    }
    return afc_result(
               afc_file_open(state_->afc, remote.c_str(), AFC_FOPEN_RW, &state_->lock_handle),
               "lock open", error) &&
           afc_result(afc_file_lock(state_->afc, state_->lock_handle, AFC_LOCK_EX),
                      "publication lock", error);
}

bool IosContainer::read(std::string_view path, std::string& output, bool& exists,
                        std::string& error)
{
    output.clear();
    exists = false;
    FileKind kind;
    std::uint64_t size = 0;
    fs::path local;
    std::string remote;
    if (!state_ || !state_->info(path, kind, size, error) ||
        !state_->resolve(path, local, remote, error))
    {
        return false;
    }
    if (kind == FileKind::missing)
    {
        return true;
    }
    if (kind != FileKind::file || size > 64U * 1024U * 1024U)
    {
        error = "The iOS state file has an unsupported type or size";
        return false;
    }
    output.resize(static_cast<std::size_t>(size));
    bool success = true;
    if (state_->simulator)
    {
        std::ifstream input(local, std::ios::binary);
        input.read(output.data(), static_cast<std::streamsize>(size));
        success = input.good() && static_cast<std::uint64_t>(input.gcount()) == size;
    }
    else
    {
        std::uint64_t handle = 0;
        success = afc_result(afc_file_open(state_->afc, remote.c_str(), AFC_FOPEN_RDONLY, &handle),
                             "file open", error);
        for (std::uint64_t offset = 0; success && offset < size;)
        {
            const auto count =
                static_cast<std::uint32_t>(std::min<std::uint64_t>(chunk_size, size - offset));
            std::uint32_t received = 0;
            success = afc_result(afc_file_read(state_->afc, handle, output.data() + offset, count,
                                               &received),
                                 "file read", error) &&
                      received == count;
            offset += received;
        }
        if (handle != 0)
        {
            const bool closed =
                afc_result(afc_file_close(state_->afc, handle), "file close", error);
            success = success && closed;
        }
    }
    if (!success)
    {
        output.clear();
        if (error.empty())
        {
            error = "The iOS state file read was incomplete";
        }
        return false;
    }
    exists = true;
    return true;
}

bool IosContainer::upload(const fs::path& source, std::string_view path, std::string& error)
{
    if (!state_ || !relative_path(path))
    {
        error = "Invalid app-relative iOS upload path";
        return false;
    }
    if (!state_->prepare_root(error))
    {
        return false;
    }
    const auto end = path.rfind('/');
    if (end != path.npos && !make_directory(path.substr(0, end), error))
    {
        return false;
    }
    fs::path local;
    std::string remote;
    FileKind kind;
    std::uint64_t size = 0;
    std::error_code code;
    if (!state_ || !state_->info(path, kind, size, error) || kind == FileKind::directory ||
        !state_->resolve(path, local, remote, error) || !tools::no_symlinks(source) ||
        !fs::is_regular_file(source, code) || code)
    {
        if (error.empty())
        {
            error = "Invalid iOS upload source or destination";
        }
        return false;
    }
    std::ifstream input(source, std::ios::binary);
    if (!input)
    {
        error = "Cannot read the iOS upload source";
        return false;
    }
    std::ofstream output;
    std::uint64_t handle = 0;
    if (state_->simulator)
    {
        output.open(local, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            error = "Cannot open the iOS simulator upload destination";
            return false;
        }
    }
    else if (!afc_result(afc_file_open(state_->afc, remote.c_str(), AFC_FOPEN_WRONLY, &handle),
                         "upload open", error))
    {
        return false;
    }
    std::vector<char> buffer(chunk_size);
    bool success = true;
    while (success && input)
    {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = static_cast<std::uint32_t>(input.gcount());
        if (state_->simulator)
        {
            output.write(buffer.data(), count);
            success = output.good();
        }
        else
        {
            for (std::uint32_t written = 0; success && written < count;)
            {
                std::uint32_t part = 0;
                success = afc_result(afc_file_write(state_->afc, handle, buffer.data() + written,
                                                    count - written, &part),
                                     "upload write", error) &&
                          part > 0 && part <= count - written;
                written += part;
            }
        }
    }
    success = success && !input.bad();
    if (state_->simulator)
    {
        output.close();
        success = success && !output.fail();
    }
    else
    {
        const bool closed = afc_result(afc_file_close(state_->afc, handle), "upload close", error);
        success = success && closed;
    }
    if (!success && error.empty())
    {
        error = "The iOS resource upload was incomplete";
    }
    return success;
}

bool IosContainer::verify(std::string_view path, std::uint64_t size, std::string_view expected,
                          std::string& error)
{
    FileKind kind;
    std::uint64_t actual = 0;
    fs::path local;
    std::string remote;
    if (!state_ || !state_->info(path, kind, actual, error) || kind != FileKind::file ||
        size != actual || expected.size() != 16 || !state_->resolve(path, local, remote, error))
    {
        if (error.empty())
        {
            error = "The staged iOS resource has an invalid type, size or hash";
        }
        return false;
    }
    std::ifstream input;
    std::uint64_t handle = 0;
    if (state_->simulator)
    {
        input.open(local, std::ios::binary);
    }
    else if (!afc_result(afc_file_open(state_->afc, remote.c_str(), AFC_FOPEN_RDONLY, &handle),
                         "verification open", error))
    {
        return false;
    }
    std::vector<char> buffer(chunk_size);
    auto value = hash::offset;
    bool success = !state_->simulator || input.good();
    for (std::uint64_t offset = 0; success && offset < size;)
    {
        const auto count =
            static_cast<std::uint32_t>(std::min<std::uint64_t>(buffer.size(), size - offset));
        std::uint32_t received = 0;
        if (state_->simulator)
        {
            input.read(buffer.data(), count);
            received = static_cast<std::uint32_t>(input.gcount());
            success = input.good();
        }
        else
        {
            success =
                afc_result(afc_file_read(state_->afc, handle, buffer.data(), count, &received),
                           "verification read", error);
        }
        success = success && received == count;
        if (success)
        {
            value = hash::fnv1a(std::string_view(buffer.data(), received), value);
            offset += received;
        }
    }
    if (handle != 0)
    {
        const bool closed =
            afc_result(afc_file_close(state_->afc, handle), "verification close", error);
        success = success && closed;
    }
    if (!success || hash::hex(value) != expected)
    {
        if (error.empty())
        {
            error = "iOS resource readback hash verification failed: " + std::string(path);
        }
        return false;
    }
    return true;
}

bool IosContainer::rename(std::string_view source, std::string_view target, std::string& error)
{
    fs::path from, to;
    std::string remote_from, remote_to;
    FileKind source_kind, target_kind;
    std::uint64_t size = 0;
    if (!state_ || !state_->resolve(source, from, remote_from, error) ||
        !state_->resolve(target, to, remote_to, error) ||
        !state_->info(source, source_kind, size, error) ||
        !state_->info(target, target_kind, size, error) || source_kind == FileKind::missing ||
        target_kind == FileKind::directory ||
        (source_kind == FileKind::directory && target_kind != FileKind::missing))
    {
        if (error.empty())
        {
            error = "Invalid iOS atomic rename source or destination";
        }
        return false;
    }
    if (!state_->simulator)
    {
        return afc_result(afc_rename_path(state_->afc, remote_from.c_str(), remote_to.c_str()),
                          "atomic rename", error);
    }
    std::error_code code;
    fs::rename(from, to, code);
    if (code)
    {
        error = "iOS simulator atomic rename failed: " + code.message();
    }
    return !code;
}

bool IosContainer::remove(std::string_view path, std::string& error)
{
    fs::path local;
    std::string remote;
    if (!state_ || !state_->resolve(path, local, remote, error))
    {
        return false;
    }
    FileKind kind;
    std::uint64_t size = 0;
    if (!state_->info(path, kind, size, error))
    {
        return false;
    }
    if (kind == FileKind::missing)
    {
        return true;
    }
    if (!state_->simulator)
    {
        return afc_result(afc_remove_path_and_contents(state_->afc, remote.c_str()),
                          "staging removal", error);
    }
    std::error_code code;
    fs::remove_all(local, code);
    if (code)
    {
        error = "Cannot remove the owned iOS simulator staging path";
    }
    return !code;
}
} // namespace engine::plugins::mcp::ios_detail
