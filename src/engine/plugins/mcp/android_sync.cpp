#include "plugins/mcp/mcp_system.hpp"

#include "editor/asset_sync/asset_sync_system.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/utils/path_utils.hpp"

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace engine
{
namespace
{

namespace fs = std::filesystem;
using Json = nlohmann::json;
constexpr int missing_remote_file = 44;

std::string nonce()
{
    static std::atomic_uint64_t counter{0};
    return std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
           std::to_string(counter.fetch_add(1));
}

std::string shell_quote(std::string_view text)
{
    std::string result = "'";
    for (const char value : text)
    {
        result += value == '\'' ? "'\\''" : std::string(1, value);
    }
    result += '\'';
    return result;
}

std::string reject_symlinks(std::string_view path)
{
    std::string checks;
    std::size_t end = 0;
    do
    {
        end = path.find('/', end);
        const auto component = end == path.npos ? path : path.substr(0, end);
        checks += "[ ! -L " + shell_quote(component) + " ] || exit 41; ";
        if (end != path.npos)
        {
            ++end;
        }
    } while (end != path.npos);
    return checks;
}

std::string hex_hash(std::uint64_t hash)
{
    constexpr char digits[] = "0123456789abcdef";
    std::string result(16, '0');
    for (std::size_t i = result.size(); i != 0; --i)
    {
        result[i - 1] = digits[hash & 15U];
        hash >>= 4U;
    }
    return result;
}

bool run_adb(const McpTarget& target, const std::vector<std::string>& command,
             const fs::path& input_file, std::string& output, std::string& error)
{
    output.clear();
    std::vector<std::string> arguments{PathUtils::to_utf8(target.adb_executable), "-s",
                                       target.android_serial};
    arguments.insert(arguments.end(), command.begin(), command.end());
    std::vector<const char*> argv;
    for (const auto& argument : arguments)
    {
        argv.push_back(argument.c_str());
    }
    argv.push_back(nullptr);
    SDL_IOStream* input = nullptr;
    if (!input_file.empty())
    {
        input = SDL_IOFromFile(PathUtils::to_utf8(input_file).c_str(), "rb");
        if (input == nullptr)
        {
            error = std::string("cannot open adb upload input: ") + SDL_GetError();
            return false;
        }
    }
    const auto properties = SDL_CreateProperties();
    const bool configured =
        properties != 0 &&
        SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv.data()) &&
        SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,
                              SDL_PROCESS_STDIO_APP) &&
        SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN,
                               true) &&
        SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER,
                              input != nullptr ? SDL_PROCESS_STDIO_REDIRECT
                                               : SDL_PROCESS_STDIO_NULL) &&
        (input == nullptr ||
         SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_STDIN_POINTER, input));
    SDL_Process* process = configured ? SDL_CreateProcessWithProperties(properties) : nullptr;
    SDL_DestroyProperties(properties);
    const bool input_closed = input == nullptr || SDL_CloseIO(input);
    if (process == nullptr)
    {
        error = std::string("cannot start adb: ") + SDL_GetError();
        return false;
    }
    std::size_t size = 0;
    int exit_code = -1;
    void* bytes = SDL_ReadProcess(process, &size, &exit_code);
    if (bytes != nullptr)
    {
        output.assign(static_cast<const char*>(bytes), size);
    }
    SDL_free(bytes);
    const bool waited = SDL_WaitProcess(process, true, &exit_code);
    SDL_DestroyProcess(process);
    if (!input_closed || !waited || exit_code != 0)
    {
        error = "adb failed for serial " + target.android_serial + ": " + output.substr(0, 8192);
        return false;
    }
    if (size > 65U * 1024U * 1024U)
    {
        error = "adb response exceeds the supported file size";
        output.clear();
        return false;
    }
    return true;
}

// exec-out 不可靠地传递设备进程退出码，因此附加每次唯一的完成回执。
// stdout 可包含任意二进制内容；只剥离最后一条匹配本次 nonce 的回执。
bool remote_command(const McpTarget& target, std::string_view script, const fs::path& input_file,
                    std::string& output, int& exit_code, std::string& error)
{
    const auto marker = "__ENGINE_ADB_" + nonce() + "__";
    const auto command = "run-as " + shell_quote(target.android_package) + " sh -c " +
                         shell_quote(script) + "; _engine_exit=$?; printf '\\n" + marker +
                         ":%s\\n' \"$_engine_exit\"";
    // exec-out 仅回传输出；上传使用非 PTY shell，保留二进制 stdin 和完成回执。
    const std::vector<std::string> arguments =
        input_file.empty() ? std::vector<std::string>{"exec-out", command}
                           : std::vector<std::string>{"shell", "-T", command};
    if (!run_adb(target, arguments, input_file, output, error))
    {
        return false;
    }
    const auto prefix = "\n" + marker + ":";
    const auto position = output.rfind(prefix);
    if (position == std::string::npos || output.empty() || output.back() != '\n')
    {
        error = "Android command ended without a complete receipt: " + output.substr(0, 8192);
        return false;
    }
    const auto first = output.data() + position + prefix.size();
    const auto last = output.data() + output.size() - 1;
    const auto parsed = std::from_chars(first, last, exit_code);
    if (parsed.ec != std::errc{} || parsed.ptr != last)
    {
        error = "Android command returned an invalid exit receipt";
        return false;
    }
    output.resize(position);
    return true;
}

bool remote_ok(const McpTarget& target, std::string_view script, const fs::path& input_file,
               std::string& output, std::string& error)
{
    int exit_code = -1;
    if (!remote_command(target, script, input_file, output, exit_code, error))
    {
        return false;
    }
    if (exit_code != 0)
    {
        error = "Android run-as command failed (a debuggable package is required): " +
                output.substr(0, 8192);
        return false;
    }
    return true;
}

bool probe(const McpTarget& target, std::string& runtime_dir, std::string& error)
{
    std::string output;
    if (!run_adb(target, {"get-state"}, {}, output, error))
    {
        return false;
    }
    while (!output.empty() && (output.back() == '\n' || output.back() == '\r'))
    {
        output.pop_back();
    }
    if (output != "device")
    {
        error = "explicit Android serial is not online: " + target.android_serial;
        return false;
    }
    if (!remote_ok(target, "[ ! -L files ] || exit 41; pwd", {}, output, error))
    {
        return false;
    }
    while (!output.empty() && (output.back() == '\n' || output.back() == '\r'))
    {
        output.pop_back();
    }
    if (!output.starts_with("/data/") || !output.ends_with("/" + target.android_package) ||
        output.find('\n') != std::string::npos)
    {
        error = "run-as returned an unexpected package working directory";
        return false;
    }
    runtime_dir = output + "/files";
    return true;
}

bool read_remote(const McpTarget& target, std::string_view path, std::string& output, bool& exists,
                 std::string& error)
{
    const auto quoted = shell_quote(path);
    const auto script = reject_symlinks(path) + "[ -e " + quoted + " ] || exit 44; [ -f " + quoted +
                        " ] || exit 45; cat " + quoted;
    int exit_code = -1;
    if (!remote_command(target, script, {}, output, exit_code, error))
    {
        return false;
    }
    exists = exit_code != missing_remote_file;
    if (exit_code == 0 || exit_code == missing_remote_file)
    {
        return true;
    }
    error = "cannot read Android app-private file: " + std::string(path) + ": " +
            output.substr(0, 8192);
    return false;
}

bool verify_remote_file(const McpTarget& target, std::string_view path, std::uint64_t size,
                        std::string_view expected_hash, std::string& error)
{
    if (expected_hash.size() != 16)
    {
        error = "Invalid staged Android file hash: " + std::string(path);
        return false;
    }
    const auto quoted = shell_quote(path);
    const auto check = reject_symlinks(path) + "[ -f " + quoted + " ] || exit 45; [ \"$(wc -c < " +
                       quoted + ")\" -eq " + std::to_string(size) + " ] || exit 46; ";
    std::string output;
    if (!remote_ok(target, check, {}, output, error))
    {
        return false;
    }
    // Android toybox 与宿主 fixture 均提供 dd；每次最多读回 1 MiB，包大小不影响内存上限。
    constexpr std::uint64_t chunk_size = 1024 * 1024;
    std::uint64_t hash = 14695981039346656037ULL;
    for (std::uint64_t offset = 0; offset < size;)
    {
        const auto expected = std::min(chunk_size, size - offset);
        const auto command = check + "dd if=" + quoted + " bs=" + std::to_string(chunk_size) +
                             " skip=" + std::to_string(offset / chunk_size) +
                             " count=1 2>/dev/null";
        if (!remote_ok(target, command, {}, output, error))
        {
            return false;
        }
        if (output.size() != expected)
        {
            error = "Android file readback returned an incomplete chunk: " + std::string(path);
            return false;
        }
        for (const unsigned char value : output)
        {
            hash = (hash ^ value) * 1099511628211ULL;
        }
        offset += expected;
    }
    if (hex_hash(hash) != expected_hash)
    {
        error = "Android staged file hash verification failed: " + std::string(path);
        return false;
    }
    return true;
}

bool upload(const McpTarget& target, const fs::path& local_file, std::string_view remote_file,
            std::string& error)
{
    const auto separator = remote_file.rfind('/');
    const auto parent = std::string(remote_file.substr(0, separator));
    const auto quoted = shell_quote(remote_file);
    std::string output;
    return remote_ok(target,
                     "umask 077; " + reject_symlinks(remote_file) + "mkdir -p " +
                         shell_quote(parent) + " && [ ! -L " + quoted + " ] && cat > " + quoted,
                     local_file, output, error);
}

bool write_text(const fs::path& path, std::string_view text)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.close();
    return !output.fail();
}

void set_reply(const McpTarget& target, std::string_view runtime_dir, Json result,
               std::string& reply)
{
    result["deviceSerial"] = target.android_serial;
    result["packageName"] = target.android_package;
    if (!runtime_dir.empty())
    {
        result["runtimeDir"] = runtime_dir;
    }
    reply = result.dump(-1, ' ', false, Json::error_handler_t::replace);
}

bool valid_manifest(const Json& manifest)
{
    return manifest.is_object() && manifest.contains("revision") &&
           manifest["revision"].is_string() && manifest.contains("entry") &&
           manifest["entry"].is_string() && manifest.contains("files") &&
           manifest["files"].is_array() && manifest.contains("deleted") &&
           manifest["deleted"].is_array();
}

} // namespace

bool McpSystem::sync_android(const fs::path& source, std::string& reply)
{
    std::string error;
    std::string runtime_dir;
    const auto token = nonce();
    const auto staging = "files/assets/.staging-" + token;
    const auto next_manifest = "files/assets/.available-" + token + ".json";
    std::error_code temporary_error;
    const auto temporary_directory = fs::temp_directory_path(temporary_error);
    const auto temporary_root =
        temporary_error ? fs::path{} : fs::canonical(temporary_directory, temporary_error);
    if (temporary_error)
    {
        set_reply(target_, runtime_dir, {{"error", "cannot locate host temporary directory"}},
                  reply);
        return false;
    }
    const auto mirror = temporary_root / ("engine-android-sync-" + token);
    bool locked = false;
    const auto cleanup = [&]()
    {
        std::error_code ignored;
        fs::remove_all(mirror, ignored);
        if (!locked)
        {
            return true;
        }
        std::string ignored_output;
        std::string cleanup_error;
        const auto script =
            "[ \"$(cat files/assets/.sync-lock/owner 2>/dev/null)\" = " + shell_quote(token) +
            " ] || exit 46; rm -rf -- " + shell_quote(staging) + "; rm -f -- " +
            shell_quote(next_manifest) +
            " files/assets/.sync-lock/owner; rmdir files/assets/.sync-lock";
        return remote_ok(target_, script, {}, ignored_output, cleanup_error);
    };
    const auto fail = [&](std::string message)
    {
        const bool cleaned = cleanup();
        if (!cleaned)
        {
            message += "; owned Android staging/lock cleanup was incomplete";
        }
        set_reply(target_, runtime_dir, {{"error", std::move(message)}}, reply);
        return false;
    };
    if (!probe(target_, runtime_dir, error))
    {
        return fail(error);
    }
    std::string output;
    const auto lock_script =
        "umask 077; [ ! -L files ] && [ ! -L files/assets ] && "
        "[ ! -L files/assets/versions ] && [ ! -L files/assets/available.json ] || exit 41; "
        "mkdir -p files/assets/versions || exit 47; "
        "mkdir files/assets/.sync-lock || { echo 'asset sync lock already exists'; exit 48; }; "
        "printf '%s' " +
        shell_quote(token) + " > files/assets/.sync-lock/owner";
    if (!remote_ok(target_, lock_script, {}, output, error))
    {
        return fail(error);
    }
    locked = true;
    bool exists = false;
    std::string baseline_text;
    if (!read_remote(target_, "files/assets/available.json", baseline_text, exists, error))
    {
        return fail(error);
    }
    const bool has_available = exists;
    Json published;
    if (has_available)
    {
        published = Json::parse(baseline_text, nullptr, false);
        if (!valid_manifest(published))
        {
            return fail("Android available manifest is invalid");
        }
    }
    std::string builtin_text;
    if (!read_remote(target_, "files/assets/builtin.json", builtin_text, exists, error))
    {
        return fail(error);
    }
    if (!has_available && !exists)
    {
        return fail("Android has no initial asset manifest; launch the installed app once first");
    }
    Json baseline = has_available ? published : Json::parse(builtin_text, nullptr, false);
    if (!valid_manifest(baseline))
    {
        return fail("Android asset baseline manifest is invalid");
    }
    if (has_available && exists)
    {
        const auto builtin = Json::parse(builtin_text, nullptr, false);
        if (!valid_manifest(builtin))
        {
            return fail("Android built-in manifest is invalid");
        }
        // 包体升级也可能新增内置路径；合并基线后由 AssetSync 去重并计算删除项。
        for (const auto& item : builtin["files"])
        {
            baseline["files"].push_back(item);
        }
        for (const auto& item : builtin["deleted"])
        {
            baseline["deleted"].push_back(item);
        }
    }
    baseline_text = baseline.dump();
    std::error_code filesystem_error;
    fs::create_directories(mirror / "assets", filesystem_error);
    if (filesystem_error || !write_text(mirror / "assets" / "available.json", baseline_text))
    {
        return fail("cannot prepare local Android asset baseline");
    }
    if (!sync_.sync(source, mirror))
    {
        return fail(sync_.last_error());
    }
    std::string manifest_text;
    const auto manifest_path = mirror / "assets" / "available.json";
    if (!ResourceSystem::read_text(manifest_path, manifest_text))
    {
        return fail("cannot read generated Android asset manifest");
    }
    const auto manifest = Json::parse(manifest_text, nullptr, false);
    if (!valid_manifest(manifest))
    {
        return fail("generated Android asset manifest is invalid");
    }
    const auto revision = sync_.revision();
    const auto version = "files/assets/versions/" + revision;
    const auto inspect = "[ ! -L " + shell_quote(version) +
                         " ] || exit 41; "
                         "if [ -d " +
                         shell_quote(version) +
                         " ]; then printf existing; "
                         "else printf missing; fi";
    if (!remote_ok(target_, inspect, {}, output, error))
    {
        return fail(error);
    }
    const bool reuse_version = output == "existing";
    if (!reuse_version && !remote_ok(target_, "mkdir " + shell_quote(staging), {}, output, error))
    {
        return fail(error);
    }
    for (const auto& item : manifest["files"])
    {
        if (!item.is_object() || !item.contains("path") || !item["path"].is_string() ||
            !item.contains("hash") || !item["hash"].is_string() || !item.contains("size") ||
            !item["size"].is_number_unsigned())
        {
            return fail("generated file manifest has invalid fields");
        }
        const auto relative = item["path"].get<std::string>();
        const auto remote_file = (reuse_version ? version : staging) + "/" + relative;
        if (!reuse_version &&
            !upload(target_,
                    mirror / "assets" / "versions" / revision / PathUtils::from_utf8(relative),
                    remote_file, error))
        {
            return fail(error);
        }
        if (!verify_remote_file(target_, remote_file, item["size"].get<std::uint64_t>(),
                                item["hash"].get<std::string>(), error))
        {
            return fail(error);
        }
    }
    if (reuse_version && published == manifest)
    {
        const bool cleaned = cleanup();
        Json result = {{"availableRevision", revision},
                       {"unchanged", true},
                       {"activation", "pending engine decision"}};
        if (!cleaned)
        {
            result["warning"] = "owned Android sync lock cleanup was incomplete";
        }
        set_reply(target_, runtime_dir, std::move(result), reply);
        return true;
    }
    if (!reuse_version &&
        !remote_ok(target_, "mv " + shell_quote(staging) + " " + shell_quote(version), {}, output,
                   error))
    {
        return fail(error);
    }
    if (!upload(target_, manifest_path, next_manifest, error) ||
        !read_remote(target_, next_manifest, output, exists, error))
    {
        return fail(error);
    }
    if (!exists || output != manifest_text)
    {
        return fail("Android staged manifest verification failed");
    }
    if (!remote_ok(target_, "mv -f " + shell_quote(next_manifest) + " files/assets/available.json",
                   {}, output, error))
    {
        return fail("Android manifest publication was not confirmed: " + error);
    }
    const bool cleaned = cleanup();
    Json result = {{"availableRevision", revision}, {"activation", "pending engine decision"}};
    if (!cleaned)
    {
        result["warning"] = "version published, but owned staging/lock cleanup was incomplete";
    }
    set_reply(target_, runtime_dir, std::move(result), reply);
    return true;
}

bool McpSystem::set_android_hot_reload(bool enabled, std::string& reply)
{
    std::string error;
    std::string runtime_dir;
    const auto token = nonce();
    const auto local = project_root_ / "cache" / (".android-control-" + token + ".json");
    const auto remote = "files/.script-control-" + token + ".json";
    const auto text = Json({{"hotReload", enabled}}).dump() + "\n";
    std::string output;
    bool exists = false;
    bool success = probe(target_, runtime_dir, error);
    if (success && !write_text(local, text))
    {
        success = false;
        error = "cannot prepare Android hot reload control";
    }
    if (success)
    {
        success = upload(target_, local, remote, error) &&
                  read_remote(target_, remote, output, exists, error);
        if (success && (!exists || output != text))
        {
            success = false;
            error = "Android hot reload control verification failed";
        }
    }
    if (success)
    {
        const auto command =
            reject_symlinks("files/script-control.json") +
            "{ [ ! -e files/script-control.json ] || [ -f files/script-control.json ]; } "
            "|| exit 45; mv -f " +
            shell_quote(remote) + " files/script-control.json";
        success = remote_ok(target_, command, {}, output, error);
        if (!success)
        {
            error = "Android hot reload control publication was not confirmed: " + error;
        }
    }
    std::error_code ignored;
    fs::remove(local, ignored);
    if (!success)
    {
        if (!runtime_dir.empty())
        {
            std::string cleanup_error;
            const bool cleaned =
                remote_ok(target_, "rm -f -- " + shell_quote(remote), {}, output, cleanup_error);
            if (!cleaned)
            {
                error += "; temporary control cleanup was incomplete";
            }
        }
        set_reply(target_, runtime_dir, {{"error", error}}, reply);
        return false;
    }
    set_reply(target_, runtime_dir,
              {{"requestedHotReload", enabled}, {"activation", "pending engine decision"}}, reply);
    return true;
}

bool McpSystem::read_android_status(std::string& reply)
{
    std::string error;
    std::string runtime_dir;
    const auto fail = [&]()
    {
        set_reply(target_, runtime_dir, {{"error", error}}, reply);
        return false;
    };
    if (!probe(target_, runtime_dir, error))
    {
        return fail();
    }
    Json result = {{"running", false},
                   {"state", "offline"},
                   {"activeRevision", ""},
                   {"availableRevision", ""}};
    std::string text;
    bool exists = false;
    if (!read_remote(target_, "files/script-status.json", text, exists, error))
    {
        return fail();
    }
    if (exists)
    {
        result = Json::parse(text, nullptr, false);
        if (!result.is_object() || !result.contains("running") || !result["running"].is_boolean())
        {
            error = "Android script status is invalid";
            return fail();
        }
        result["state"] = result["running"].get<bool>() ? "lastReportedRunning" : "offline";
    }
    if (!read_remote(target_, "files/assets/available.json", text, exists, error))
    {
        return fail();
    }
    result["availableRevision"] = "";
    if (exists)
    {
        const auto available = Json::parse(text, nullptr, false);
        if (!valid_manifest(available))
        {
            error = "Android available manifest is invalid";
            return fail();
        }
        result["availableRevision"] = available["revision"];
    }
    result["statusSource"] =
        "device files: availableRevision is published; runtime fields are the last engine report";
    set_reply(target_, runtime_dir, std::move(result), reply);
    return true;
}

} // namespace engine
